#include "Pico/Engine/Replication.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/MemoryTracker.h"
#include "Pico/Core/ScopeExit.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Net/NetPacket.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace Pico
{
namespace
{
constexpr uint32 MaxRpcCallsPerConnectionPerFrame = 32;
constexpr uint16 ReplicationMagic = 0x5052;
constexpr uint8 ReplicationVersion = 2;
constexpr std::size_t MaxReplicatedFields = 64;
constexpr std::size_t MaxReplicationStringBytes = 255;
constexpr std::size_t MaxPendingObjectReferences = 256;
constexpr std::size_t MaxRpcArguments = 8;

enum class EReplicationMessageType : uint8
{
    Spawn = 1,
    Delta = 2,
    Destroy = 3,
    Rpc = 4,
    CharacterMove = 5,
    CharacterCorrection = 6,
    CharacterSnapshot = 7
};

struct FFieldValue
{
    uint16 FieldId = 0;
    EPropertyType Type = EPropertyType::Int32;
    std::vector<uint8> Data;
};

struct FPendingReference
{
    FObjectHandle OwnerHandle;
    const PProperty* Property = nullptr;
    FNetObjectId TargetNetId;
    FName RepNotifyFunction;
};

bool WriteString(FNetByteWriter& Writer, std::string_view Value)
{
    return Value.size() <= MaxReplicationStringBytes
        && Writer.WriteUInt8(static_cast<uint8>(Value.size()))
        && Writer.WriteBytes(std::span<const uint8>(
            reinterpret_cast<const uint8*>(Value.data()), Value.size()));
}

bool ReadString(FNetByteReader& Reader, std::string& OutValue)
{
    uint8 Length = 0;
    std::vector<uint8> Bytes;
    if (!Reader.ReadUInt8(Length) || !Reader.ReadBytes(Length, Bytes))
    {
        return false;
    }
    OutValue.assign(Bytes.begin(), Bytes.end());
    return true;
}

bool WriteFloat(FNetByteWriter& Writer, float Value)
{
    return Writer.WriteUInt32(std::bit_cast<uint32>(Value));
}

bool ReadFloat(FNetByteReader& Reader, float& OutValue)
{
    uint32 Bits = 0;
    if (!Reader.ReadUInt32(Bits)) return false;
    OutValue = std::bit_cast<float>(Bits);
    return true;
}

bool WriteDouble(FNetByteWriter& Writer, double Value)
{
    return Writer.WriteUInt64(std::bit_cast<uint64>(Value));
}

bool ReadDouble(FNetByteReader& Reader, double& OutValue)
{
    uint64 Bits = 0;
    if (!Reader.ReadUInt64(Bits)) return false;
    OutValue = std::bit_cast<double>(Bits);
    return true;
}

bool WriteVector(FNetByteWriter& Writer, const FVector3& Value)
{
    return WriteFloat(Writer, Value.X)
        && WriteFloat(Writer, Value.Y)
        && WriteFloat(Writer, Value.Z);
}

bool ReadVector(FNetByteReader& Reader, FVector3& OutValue)
{
    return ReadFloat(Reader, OutValue.X)
        && ReadFloat(Reader, OutValue.Y)
        && ReadFloat(Reader, OutValue.Z);
}

bool WriteTransform(FNetByteWriter& Writer, const FTransform& Value)
{
    return WriteFloat(Writer, Value.Rotation.X)
        && WriteFloat(Writer, Value.Rotation.Y)
        && WriteFloat(Writer, Value.Rotation.Z)
        && WriteFloat(Writer, Value.Rotation.W)
        && WriteVector(Writer, Value.Translation)
        && WriteVector(Writer, Value.Scale);
}

bool ReadTransform(FNetByteReader& Reader, FTransform& OutValue)
{
    return ReadFloat(Reader, OutValue.Rotation.X)
        && ReadFloat(Reader, OutValue.Rotation.Y)
        && ReadFloat(Reader, OutValue.Rotation.Z)
        && ReadFloat(Reader, OutValue.Rotation.W)
        && ReadVector(Reader, OutValue.Translation)
        && ReadVector(Reader, OutValue.Scale);
}

bool EncodeRpcArgument(
    const FFunctionValue& Value,
    const FFunctionValueDescriptor& Descriptor,
    const FNetObjectRegistry& Registry,
    std::vector<uint8>& OutData)
{
    if (!IsFunctionValueCompatible(Value, Descriptor)) return false;
    FNetByteWriter Writer;
    switch (Descriptor.Type)
    {
    case EFunctionValueType::Int32:
        if (!Writer.WriteUInt32(static_cast<uint32>(std::get<int32>(Value)))) return false;
        break;
    case EFunctionValueType::Float:
        if (!WriteFloat(Writer, std::get<float>(Value))) return false;
        break;
    case EFunctionValueType::Bool:
        if (!Writer.WriteUInt8(std::get<bool>(Value) ? 1 : 0)) return false;
        break;
    case EFunctionValueType::Name:
        if (!WriteString(Writer, std::get<FName>(Value).ToString())) return false;
        break;
    case EFunctionValueType::String:
        if (!WriteString(Writer, std::get<std::string>(Value))) return false;
        break;
    case EFunctionValueType::Vector3:
        if (!WriteVector(Writer, std::get<FVector3>(Value))) return false;
        break;
    case EFunctionValueType::Rotator:
    {
        const FRotator& ValueRotator = std::get<FRotator>(Value);
        if (!WriteFloat(Writer, ValueRotator.Pitch)
            || !WriteFloat(Writer, ValueRotator.Yaw)
            || !WriteFloat(Writer, ValueRotator.Roll)) return false;
        break;
    }
    case EFunctionValueType::Transform:
        if (!WriteTransform(Writer, std::get<FTransform>(Value))) return false;
        break;
    case EFunctionValueType::AssetPath:
        if (!WriteString(Writer, std::get<FAssetPath>(Value).ToString())) return false;
        break;
    case EFunctionValueType::Object:
    {
        PObject* Object = std::get<PObject*>(Value);
        PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
            ? static_cast<PActor*>(Object) : nullptr;
        const FNetObjectId NetId = Actor != nullptr
            ? Registry.FindNetId(Actor->GetHandle()) : FNetObjectId {};
        if (Object != nullptr && !NetId.IsValid()) return false;
        if (!Writer.WriteUInt32(NetId.Value)) return false;
        break;
    }
    case EFunctionValueType::Void:
        return false;
    }
    OutData = Writer.GetBytes();
    return true;
}

bool DecodeRpcArgument(
    std::span<const uint8> Data,
    const FFunctionValueDescriptor& Descriptor,
    const FNetObjectRegistry& Registry,
    FFunctionValue& OutValue)
{
    FNetByteReader Reader(Data);
    switch (Descriptor.Type)
    {
    case EFunctionValueType::Int32:
    {
        uint32 Value = 0;
        if (!Reader.ReadUInt32(Value)) return false;
        OutValue = static_cast<int32>(Value);
        break;
    }
    case EFunctionValueType::Float:
    {
        float Value = 0.0f;
        if (!ReadFloat(Reader, Value)) return false;
        OutValue = Value;
        break;
    }
    case EFunctionValueType::Bool:
    {
        uint8 Value = 0;
        if (!Reader.ReadUInt8(Value) || Value > 1) return false;
        OutValue = Value != 0;
        break;
    }
    case EFunctionValueType::Name:
    case EFunctionValueType::String:
    case EFunctionValueType::AssetPath:
    {
        std::string Value;
        if (!ReadString(Reader, Value)) return false;
        if (Descriptor.Type == EFunctionValueType::Name) OutValue = FName(Value);
        else if (Descriptor.Type == EFunctionValueType::String) OutValue = Value;
        else
        {
            FAssetPath Path;
            if (!FAssetPath::TryParse(Value, Path)) return false;
            OutValue = Path;
        }
        break;
    }
    case EFunctionValueType::Vector3:
    {
        FVector3 Value;
        if (!ReadVector(Reader, Value)) return false;
        OutValue = Value;
        break;
    }
    case EFunctionValueType::Rotator:
    {
        FRotator Value;
        if (!ReadFloat(Reader, Value.Pitch)
            || !ReadFloat(Reader, Value.Yaw)
            || !ReadFloat(Reader, Value.Roll)) return false;
        OutValue = Value;
        break;
    }
    case EFunctionValueType::Transform:
    {
        FTransform Value;
        if (!ReadTransform(Reader, Value)) return false;
        OutValue = Value;
        break;
    }
    case EFunctionValueType::Object:
    {
        uint32 RawNetId = 0;
        if (!Reader.ReadUInt32(RawNetId)) return false;
        PActor* Actor = RawNetId != 0
            ? Registry.ResolveActor({RawNetId}) : nullptr;
        if (RawNetId != 0 && Actor == nullptr) return false;
        const PClass* RequiredClass = Descriptor.ResolveObjectClass();
        if (Actor != nullptr && RequiredClass != nullptr
            && !Actor->IsA(RequiredClass)) return false;
        OutValue = static_cast<PObject*>(Actor);
        break;
    }
    case EFunctionValueType::Void:
        return false;
    }
    return Reader.GetRemainingBytes() == 0;
}

std::vector<uint8> EncodeTransform(const FTransform& Transform)
{
    FNetByteWriter Writer;
    return WriteTransform(Writer, Transform)
        ? Writer.GetBytes() : std::vector<uint8> {};
}

bool DecodeTransform(std::span<const uint8> Bytes, FTransform& OutTransform)
{
    FNetByteReader Reader(Bytes);
    return ReadTransform(Reader, OutTransform)
        && Reader.GetRemainingBytes() == 0;
}

uint32 HashBytes(uint32 Hash, std::string_view Value)
{
    for (const unsigned char Byte : Value)
    {
        Hash ^= Byte;
        Hash *= 16777619u;
    }
    return Hash;
}

bool ShouldReplicate(
    EReplicationCondition Condition,
    bool bInitial,
    bool bIsOwner)
{
    switch (Condition)
    {
    case EReplicationCondition::Always: return true;
    case EReplicationCondition::InitialOnly: return bInitial;
    case EReplicationCondition::OwnerOnly: return bIsOwner;
    case EReplicationCondition::SkipOwner: return !bIsOwner;
    }
    return false;
}

bool EncodePropertyValue(
    const PProperty& Property,
    const PObject* Object,
    const FNetObjectRegistry& Registry,
    std::vector<uint8>& OutData)
{
    FNetByteWriter Writer;
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        if (!Property.GetValue(Object, Value)
            || !Writer.WriteUInt32(static_cast<uint32>(Value))) return false;
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (!Property.GetValue(Object, Value) || !WriteFloat(Writer, Value)) return false;
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        if (!Property.GetValue(Object, Value)
            || !Writer.WriteUInt8(Value ? 1 : 0)) return false;
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (!Property.GetValue(Object, Value) || !WriteVector(Writer, Value)) return false;
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (!Property.GetValue(Object, Value)
            || !WriteFloat(Writer, Value.Pitch)
            || !WriteFloat(Writer, Value.Yaw)
            || !WriteFloat(Writer, Value.Roll)) return false;
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        if (!Property.GetValue(Object, Value) || !WriteTransform(Writer, Value)) return false;
        break;
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        if (!Property.GetValue(Object, Value)
            || !WriteString(Writer, Value.ToString())) return false;
        break;
    }
    case EPropertyType::Object:
    {
        const FNetObjectId NetId = Registry.FindNetId(
            Property.GetReferencedObjectHandle(Object));
        if (!Writer.WriteUInt32(NetId.Value)) return false;
        break;
    }
    case EPropertyType::DynamicMulticastDelegate:
        return false;
    }
    OutData = Writer.GetBytes();
    return true;
}

bool ReadObjectNetId(std::span<const uint8> Data, FNetObjectId& OutNetId)
{
    FNetByteReader Reader(Data);
    return Reader.ReadUInt32(OutNetId.Value)
        && Reader.GetRemainingBytes() == 0;
}

bool ApplyNonObjectProperty(
    const PProperty& Property,
    PObject* Object,
    std::span<const uint8> Data)
{
    FNetByteReader Reader(Data);
    bool bApplied = false;
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        uint32 Raw = 0;
        bApplied = Reader.ReadUInt32(Raw)
            && Property.SetValueSilently(Object, static_cast<int32>(Raw));
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        bApplied = ReadFloat(Reader, Value)
            && Property.SetValueSilently(Object, Value);
        break;
    }
    case EPropertyType::Bool:
    {
        uint8 Raw = 0;
        bApplied = Reader.ReadUInt8(Raw) && Raw <= 1
            && Property.SetValueSilently(Object, Raw != 0);
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        bApplied = ReadVector(Reader, Value)
            && Property.SetValueSilently(Object, Value);
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        bApplied = ReadFloat(Reader, Value.Pitch)
            && ReadFloat(Reader, Value.Yaw)
            && ReadFloat(Reader, Value.Roll)
            && Property.SetValueSilently(Object, Value);
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        bApplied = ReadTransform(Reader, Value)
            && Property.SetValueSilently(Object, Value);
        break;
    }
    case EPropertyType::AssetPath:
    {
        std::string Text;
        FAssetPath Value;
        bApplied = ReadString(Reader, Text)
            && (Text.empty() || FAssetPath::TryParse(Text, Value))
            && Property.SetValueSilently(Object, Value);
        break;
    }
    case EPropertyType::Object:
    case EPropertyType::DynamicMulticastDelegate:
        return false;
    }
    return bApplied && Reader.GetRemainingBytes() == 0;
}

bool ValidatePropertyData(
    const PProperty& Property,
    std::span<const uint8> Data,
    const FNetObjectRegistry& Registry)
{
    FNetByteReader Reader(Data);
    bool bValid = false;
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        uint32 Value = 0;
        bValid = Reader.ReadUInt32(Value);
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        bValid = ReadFloat(Reader, Value);
        break;
    }
    case EPropertyType::Bool:
    {
        uint8 Value = 0;
        bValid = Reader.ReadUInt8(Value) && Value <= 1;
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        bValid = ReadVector(Reader, Value);
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        bValid = ReadFloat(Reader, Value.Pitch)
            && ReadFloat(Reader, Value.Yaw)
            && ReadFloat(Reader, Value.Roll);
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        bValid = ReadTransform(Reader, Value);
        break;
    }
    case EPropertyType::AssetPath:
    {
        std::string Text;
        FAssetPath Value;
        bValid = ReadString(Reader, Text)
            && (Text.empty() || FAssetPath::TryParse(Text, Value));
        break;
    }
    case EPropertyType::Object:
    {
        FNetObjectId NetId;
        bValid = Reader.ReadUInt32(NetId.Value);
        PActor* Target = NetId.IsValid() ? Registry.ResolveActor(NetId) : nullptr;
        bValid = bValid && (Target == nullptr
            || (Property.GetReferencedObjectClass() != nullptr
                && Target->IsA(Property.GetReferencedObjectClass())));
        break;
    }
    case EPropertyType::DynamicMulticastDelegate:
        return false;
    }
    return bValid && Reader.GetRemainingBytes() == 0;
}

bool ValidateIncomingValues(
    const FReplicationSchema& Schema,
    const std::vector<FFieldValue>& Values,
    const FNetObjectRegistry& Registry)
{
    std::vector<uint16> SeenFields;
    for (const FFieldValue& Value : Values)
    {
        const FReplicationFieldDescriptor* Field = Schema.FindField(Value.FieldId);
        if (Field == nullptr || Field->Property == nullptr
            || Field->Property->GetType() != Value.Type
            || std::find(SeenFields.begin(), SeenFields.end(), Value.FieldId)
                != SeenFields.end()
            || !ValidatePropertyData(*Field->Property, Value.Data, Registry))
        {
            return false;
        }
        SeenFields.push_back(Value.FieldId);
    }
    return true;
}

bool WriteFieldValues(
    FNetByteWriter& Writer,
    const std::vector<FFieldValue>& Values)
{
    if (Values.size() > MaxReplicatedFields
        || !Writer.WriteUInt8(static_cast<uint8>(Values.size()))) return false;
    for (const FFieldValue& Value : Values)
    {
        if (Value.Data.size() > std::numeric_limits<uint16>::max()
            || !Writer.WriteUInt16(Value.FieldId)
            || !Writer.WriteUInt8(static_cast<uint8>(Value.Type))
            || !Writer.WriteUInt16(static_cast<uint16>(Value.Data.size()))
            || !Writer.WriteBytes(Value.Data)) return false;
    }
    return true;
}

bool ReadFieldValues(
    FNetByteReader& Reader,
    std::vector<FFieldValue>& OutValues)
{
    uint8 Count = 0;
    if (!Reader.ReadUInt8(Count) || Count > MaxReplicatedFields) return false;
    OutValues.clear();
    OutValues.reserve(Count);
    for (uint8 Index = 0; Index < Count; ++Index)
    {
        FFieldValue Value;
        uint8 RawType = 0;
        uint16 Size = 0;
        if (!Reader.ReadUInt16(Value.FieldId)
            || !Reader.ReadUInt8(RawType)
            || RawType > static_cast<uint8>(EPropertyType::DynamicMulticastDelegate)
            || !Reader.ReadUInt16(Size)
            || !Reader.ReadBytes(Size, Value.Data)) return false;
        Value.Type = static_cast<EPropertyType>(RawType);
        OutValues.push_back(std::move(Value));
    }
    return true;
}

bool WriteMessageHeader(FNetByteWriter& Writer, EReplicationMessageType Type)
{
    return Writer.WriteUInt16(ReplicationMagic)
        && Writer.WriteUInt8(ReplicationVersion)
        && Writer.WriteUInt8(static_cast<uint8>(Type));
}

bool WriteCharacterState(
    FNetByteWriter& Writer,
    const FCharacterNetworkState& State)
{
    const std::vector<uint8> Transform = EncodeTransform(State.State.Transform);
    return !Transform.empty()
        && Transform.size() <= std::numeric_limits<uint16>::max()
        && Writer.WriteUInt32(State.ServerTick)
        && WriteDouble(Writer, State.ServerTimeSeconds)
        && Writer.WriteUInt32(State.LastProcessedMove)
        && WriteDouble(Writer, State.LastProcessedMoveClientTimeSeconds)
        && Writer.WriteUInt64(State.PolicyHash)
        && Writer.WriteUInt16(static_cast<uint16>(Transform.size()))
        && Writer.WriteBytes(Transform)
        && WriteVector(Writer, State.State.Velocity)
        && Writer.WriteUInt8(static_cast<uint8>(State.State.MovementMode));
}

bool ReadCharacterState(
    FNetByteReader& Reader,
    FCharacterNetworkState& OutState)
{
    uint16 TransformSize = 0;
    std::vector<uint8> Transform;
    uint8 RawMode = 0;
    if (!Reader.ReadUInt32(OutState.ServerTick)
        || !ReadDouble(Reader, OutState.ServerTimeSeconds)
        || !Reader.ReadUInt32(OutState.LastProcessedMove)
        || !ReadDouble(
            Reader, OutState.LastProcessedMoveClientTimeSeconds)
        || !Reader.ReadUInt64(OutState.PolicyHash)
        || !Reader.ReadUInt16(TransformSize)
        || !Reader.ReadBytes(TransformSize, Transform)
        || !DecodeTransform(Transform, OutState.State.Transform)
        || !ReadVector(Reader, OutState.State.Velocity)
        || !Reader.ReadUInt8(RawMode)
        || RawMode > static_cast<uint8>(EMovementMode::Falling))
        return false;
    OutState.State.MovementMode = static_cast<EMovementMode>(RawMode);
    return std::isfinite(OutState.ServerTimeSeconds)
        && OutState.ServerTimeSeconds >= 0.0
        && std::isfinite(OutState.LastProcessedMoveClientTimeSeconds)
        && OutState.LastProcessedMoveClientTimeSeconds >= 0.0;
}

bool BuildCharacterStateMessage(
    EReplicationMessageType Type,
    FNetObjectId NetId,
    const FCharacterNetworkState& State,
    std::vector<uint8>& OutMessage)
{
    FNetByteWriter Writer;
    if (!WriteMessageHeader(Writer, Type)
        || !Writer.WriteUInt32(NetId.Value)
        || !WriteCharacterState(Writer, State))
        return false;
    OutMessage = Writer.GetBytes();
    return true;
}

PActor* FindUnboundStartupActor(
    PWorld* World,
    const PClass* Class,
    FName Name)
{
    if (World == nullptr) return nullptr;
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr && Actor->GetClass() == Class
                && Actor->GetName() == Name
                && !Actor->GetNetObjectId().IsValid()) return Actor;
        }
    }
    return nullptr;
}
}

FReplicationSchema FReplicationSchema::Build(const PClass* InClass)
{
    FReplicationSchema Result;
    Result.Class = InClass;
    if (InClass == nullptr) return Result;

    std::vector<const PClass*> Hierarchy;
    for (const PClass* Current = InClass; Current != nullptr;
        Current = Current->GetSuperClass())
    {
        Hierarchy.push_back(Current);
    }
    std::reverse(Hierarchy.begin(), Hierarchy.end());

    uint32 Hash = HashBytes(2166136261u, InClass->GetName().ToString());
    uint16 FieldId = 1;
    for (const PClass* Current : Hierarchy)
    {
        for (const PProperty& Property : Current->GetProperties())
        {
            if (!Property.HasAnyFlags(EPropertyFlags::Replicated)
                || Property.GetType() == EPropertyType::DynamicMulticastDelegate)
            {
                continue;
            }
            if (Result.Fields.size() >= MaxReplicatedFields) return {};
            FReplicationFieldDescriptor Descriptor;
            Descriptor.FieldId = FieldId++;
            Descriptor.Property = &Property;
            Descriptor.Condition =
                Property.GetMetadata().ReplicationCondition;
            Descriptor.RepNotifyFunction =
                Property.GetMetadata().RepNotifyFunction;
            if (!Descriptor.RepNotifyFunction.IsNone())
            {
                const PFunction* Notify = InClass->FindFunction(
                    Descriptor.RepNotifyFunction);
                if (Notify == nullptr || !Notify->GetParameters().empty())
                    return {};
            }
            Result.Fields.push_back(Descriptor);
            Hash = HashBytes(Hash, Property.GetName().ToString());
            Hash ^= static_cast<uint32>(Property.GetType());
            Hash *= 16777619u;
            Hash ^= static_cast<uint32>(Descriptor.Condition);
            Hash *= 16777619u;
            Hash = HashBytes(Hash, Descriptor.RepNotifyFunction.ToString());
        }
    }
    Result.Hash = Hash == 0 ? 1 : Hash;
    return Result;
}

const FReplicationFieldDescriptor* FReplicationSchema::FindField(
    uint16 FieldId) const
{
    const auto It = std::find_if(
        Fields.begin(), Fields.end(),
        [FieldId](const FReplicationFieldDescriptor& Field)
        {
            return Field.FieldId == FieldId;
        });
    return It != Fields.end() ? &*It : nullptr;
}

FNetObjectId FNetObjectRegistry::RegisterAuthorityObject(PActor* Actor)
{
    if (Actor == nullptr) return {};
    const FNetObjectId Existing = FindNetId(Actor->GetHandle());
    if (Existing.IsValid()) return Existing;
    FNetObjectId NetId = NextAuthorityId;
    if (++NextAuthorityId.Value == 0) ++NextAuthorityId.Value;
    Entries.push_back({NetId, Actor->GetHandle()});
    Actor->SetNetObjectId(NetId);
    return NetId;
}

bool FNetObjectRegistry::RegisterRemoteObject(
    FNetObjectId NetId,
    PActor* Actor)
{
    const bool bNetIdAlreadyRegistered = std::any_of(
        Entries.begin(), Entries.end(),
        [NetId](const FEntry& Entry) { return Entry.NetId == NetId; });
    if (!NetId.IsValid() || Actor == nullptr
        || bNetIdAlreadyRegistered
        || FindNetId(Actor->GetHandle()).IsValid()) return false;
    Entries.push_back({NetId, Actor->GetHandle()});
    Actor->SetNetObjectId(NetId);
    return true;
}

FNetObjectId FNetObjectRegistry::FindNetId(FObjectHandle Handle) const
{
    if (!Handle.IsValid()) return {};
    const auto It = std::find_if(
        Entries.begin(), Entries.end(),
        [Handle](const FEntry& Entry) { return Entry.Handle == Handle; });
    return It != Entries.end() ? It->NetId : FNetObjectId {};
}

PActor* FNetObjectRegistry::ResolveActor(FNetObjectId NetId) const
{
    if (!NetId.IsValid()) return nullptr;
    const auto It = std::find_if(
        Entries.begin(), Entries.end(),
        [NetId](const FEntry& Entry) { return Entry.NetId == NetId; });
    if (It == Entries.end()) return nullptr;
    PObject* Object = ResolveObject(It->Handle);
    return Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
}

bool FNetObjectRegistry::RemoveByNetId(FNetObjectId NetId)
{
    PActor* Actor = ResolveActor(NetId);
    if (Actor != nullptr) Actor->SetNetObjectId({});
    return std::erase_if(Entries,
        [NetId](const FEntry& Entry) { return Entry.NetId == NetId; }) > 0;
}

void FNetObjectRegistry::Reset()
{
    for (const FEntry& Entry : Entries)
    {
        if (PObject* Object = ResolveObject(Entry.Handle);
            Object != nullptr && Object->IsA(PActor::StaticClass()))
        {
            static_cast<PActor*>(Object)->SetNetObjectId({});
        }
    }
    Entries.clear();
    NextAuthorityId.Value = 1;
}

struct FReplicationSystem::FImpl
{
    struct FChannel
    {
        FNetConnectionId ConnectionId;
        FNetObjectId NetObjectId;
        FObjectHandle ActorHandle;
        EActorChannelState State = EActorChannelState::PendingOpen;
        std::vector<FFieldValue> Baseline;
        std::vector<uint8> TransformBaseline;
        std::vector<FFieldValue> PendingValues;
        std::vector<uint8> PendingTransform;
        uint32 PendingReliableId = 0;
        EReplicationMessageType PendingType = EReplicationMessageType::Spawn;
    };

    std::vector<FChannel> Channels;
    std::vector<FPendingReference> PendingReferences;
    struct FOwnership
    {
        FObjectHandle ActorHandle;
        FNetConnectionId ConnectionId;
    };
    std::vector<FOwnership> Ownership;
    struct FRpcFrameCount
    {
        FNetConnectionId ConnectionId;
        uint32 Count = 0;
    };
    std::vector<FRpcFrameCount> RpcFrameCounts;
};

namespace
{
FReplicationSystem::FImpl::FChannel* FindChannel(
    FReplicationSystem::FImpl& Impl,
    FNetConnectionId ConnectionId,
    FNetObjectId NetObjectId)
{
    const auto It = std::find_if(
        Impl.Channels.begin(), Impl.Channels.end(),
        [ConnectionId, NetObjectId](const auto& Channel)
        {
            return Channel.ConnectionId == ConnectionId
                && Channel.NetObjectId == NetObjectId;
        });
    return It != Impl.Channels.end() ? &*It : nullptr;
}

std::vector<FFieldValue> CaptureValues(
    const FReplicationSchema& Schema,
    const PActor* Actor,
    const FNetObjectRegistry& Registry,
    bool bInitial,
    bool bIsOwner)
{
    std::vector<FFieldValue> Result;
    for (const FReplicationFieldDescriptor& Field : Schema.GetFields())
    {
        if (Field.Property == nullptr
            || !ShouldReplicate(Field.Condition, bInitial, bIsOwner)) continue;
        FFieldValue Value;
        Value.FieldId = Field.FieldId;
        Value.Type = Field.Property->GetType();
        if (EncodePropertyValue(*Field.Property, Actor, Registry, Value.Data))
        {
            Result.push_back(std::move(Value));
        }
    }
    return Result;
}

const FFieldValue* FindValue(
    const std::vector<FFieldValue>& Values,
    uint16 FieldId)
{
    const auto It = std::find_if(
        Values.begin(), Values.end(),
        [FieldId](const FFieldValue& Value)
        {
            return Value.FieldId == FieldId;
        });
    return It != Values.end() ? &*It : nullptr;
}

std::vector<FFieldValue> BuildDelta(
    const std::vector<FFieldValue>& Current,
    const std::vector<FFieldValue>& Baseline)
{
    std::vector<FFieldValue> Result;
    for (const FFieldValue& Value : Current)
    {
        const FFieldValue* Previous = FindValue(Baseline, Value.FieldId);
        if (Previous == nullptr || Previous->Type != Value.Type
            || Previous->Data != Value.Data) Result.push_back(Value);
    }
    return Result;
}

void MergeBaseline(
    std::vector<FFieldValue>& Baseline,
    const std::vector<FFieldValue>& Values)
{
    for (const FFieldValue& Value : Values)
    {
        const auto It = std::find_if(
            Baseline.begin(), Baseline.end(),
            [&Value](const FFieldValue& Existing)
            {
                return Existing.FieldId == Value.FieldId;
            });
        if (It == Baseline.end()) Baseline.push_back(Value);
        else *It = Value;
    }
}

bool QueueSpawn(
    FReplicationSystem::FImpl::FChannel& Channel,
    PActor* Actor,
    const FReplicationSchema& Schema,
    const FNetObjectRegistry& Registry,
    const FReplicationSystem::FQueueReliable& Queue,
    bool bIsOwner)
{
    FNetByteWriter Writer;
    const std::vector<FFieldValue> Values = CaptureValues(
        Schema, Actor, Registry, true, bIsOwner);
    const std::vector<uint8> Transform = Actor->GetRootComponent() != nullptr
        ? EncodeTransform(Actor->GetActorTransform())
        : std::vector<uint8> {};
    if (!WriteMessageHeader(Writer, EReplicationMessageType::Spawn)
        || !Writer.WriteUInt32(Channel.NetObjectId.Value)
        || !WriteString(Writer, Actor->GetClass()->GetName().ToString())
        || !WriteString(Writer, Actor->GetName().ToString())
        || !Writer.WriteUInt8(bIsOwner ? 1 : 0)
        || !Writer.WriteUInt32(Schema.GetHash())
        || !Writer.WriteUInt16(static_cast<uint16>(Transform.size()))
        || !Writer.WriteBytes(Transform)
        || !WriteFieldValues(Writer, Values)) return false;

    uint32 ReliableId = 0;
    if (!Queue(Writer.GetBytes(), &ReliableId)) return false;
    Channel.PendingReliableId = ReliableId;
    Channel.PendingType = EReplicationMessageType::Spawn;
    Channel.PendingValues = Values;
    Channel.PendingTransform = Transform;
    return true;
}

bool QueueDelta(
    FReplicationSystem::FImpl::FChannel& Channel,
    PActor* Actor,
    const FReplicationSchema& Schema,
    const FNetObjectRegistry& Registry,
    const FReplicationSystem::FQueueReliable& Queue,
    bool bIsOwner)
{
    const std::vector<FFieldValue> Current = CaptureValues(
        Schema, Actor, Registry, false, bIsOwner);
    const std::vector<FFieldValue> Delta = BuildDelta(
        Current, Channel.Baseline);
    const std::vector<uint8> Transform = Actor->GetRootComponent() != nullptr
        ? EncodeTransform(Actor->GetActorTransform())
        : std::vector<uint8> {};
    const bool bTransformChanged = !Transform.empty()
        && Transform != Channel.TransformBaseline;
    if (Delta.empty() && !bTransformChanged) return false;

    FNetByteWriter Writer;
    if (!WriteMessageHeader(Writer, EReplicationMessageType::Delta)
        || !Writer.WriteUInt32(Channel.NetObjectId.Value)
        || !Writer.WriteUInt32(Schema.GetHash())
        || !Writer.WriteUInt8(bTransformChanged ? 1 : 0)
        || (bTransformChanged
            && (!Writer.WriteUInt16(static_cast<uint16>(Transform.size()))
                || !Writer.WriteBytes(Transform)))
        || !WriteFieldValues(Writer, Delta)) return false;

    uint32 ReliableId = 0;
    if (!Queue(Writer.GetBytes(), &ReliableId)) return false;
    Channel.PendingReliableId = ReliableId;
    Channel.PendingType = EReplicationMessageType::Delta;
    Channel.PendingValues = Delta;
    Channel.PendingTransform = bTransformChanged
        ? Transform : std::vector<uint8> {};
    return true;
}

bool QueueDestroy(
    FReplicationSystem::FImpl::FChannel& Channel,
    const FReplicationSystem::FQueueReliable& Queue)
{
    FNetByteWriter Writer;
    if (!WriteMessageHeader(Writer, EReplicationMessageType::Destroy)
        || !Writer.WriteUInt32(Channel.NetObjectId.Value)) return false;
    uint32 ReliableId = 0;
    if (!Queue(Writer.GetBytes(), &ReliableId)) return false;
    Channel.PendingReliableId = ReliableId;
    Channel.PendingType = EReplicationMessageType::Destroy;
    Channel.PendingValues.clear();
    Channel.PendingTransform.clear();
    Channel.State = EActorChannelState::PendingClose;
    return true;
}
}

void FReplicationSystem::SetWorld(PWorld* InWorld)
{
    if (World == InWorld) return;
    Reset();
    World = InWorld;
}

void FReplicationSystem::BeginNetworkFrame()
{
    if (Impl != nullptr) Impl->RpcFrameCounts.clear();
}

void FReplicationSystem::Reset()
{
    if (Impl != nullptr)
    {
        Impl->Channels.clear();
        Impl->PendingReferences.clear();
        Impl->Ownership.clear();
        Impl->RpcFrameCounts.clear();
    }
    ObjectRegistry.Reset();
    Statistics = {};
    World = nullptr;
    PublishMemoryStatistics();
}

void FReplicationSystem::ReplicateServerConnection(
    FNetConnectionId ConnectionId,
    const FQueueReliable& QueueReliable,
    const FQueueUnreliable& QueueUnreliable)
{
    if (World == nullptr || !ConnectionId.IsValid() || !QueueReliable) return;
    if (Impl == nullptr) Impl = std::make_shared<FImpl>();

    FMemoryTracker& MemoryTracker = FMemoryTracker::Get();
    const auto ResetSchemaMemory = MakeScopeExit([&MemoryTracker]()
    {
        MemoryTracker.Report(EMemoryTag::ReplicationSchema, 0, 0, 0);
    });
    std::size_t SchemaPeakBytes = 0;
    std::size_t SchemaPeakReservedBytes = 0;
    std::size_t SchemaPeakFieldCount = 0;

    std::vector<PActor*> ReplicatedActors;
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr && Actor->GetIsReplicated()
                && !Actor->IsPendingDestroy()) ReplicatedActors.push_back(Actor);
        }
    }

    for (PActor* Actor : ReplicatedActors)
    {
        ObjectRegistry.RegisterAuthorityObject(Actor);
    }

    for (PActor* Actor : ReplicatedActors)
    {
        const bool bIsOwner =
            GetActorOwningConnection(Actor) == ConnectionId;
        if (Actor->IsOnlyRelevantToOwner() && !bIsOwner) continue;
        const FNetObjectId NetId = ObjectRegistry.FindNetId(
            Actor->GetHandle());
        if (!NetId.IsValid()) continue;
        FImpl::FChannel* Channel = FindChannel(*Impl, ConnectionId, NetId);
        if (Channel == nullptr)
        {
            FImpl::FChannel NewChannel;
            NewChannel.ConnectionId = ConnectionId;
            NewChannel.NetObjectId = NetId;
            NewChannel.ActorHandle = Actor->GetHandle();
            Impl->Channels.push_back(std::move(NewChannel));
            Channel = &Impl->Channels.back();
        }
        if (Channel->PendingReliableId != 0) continue;
        const FReplicationSchema Schema =
            FReplicationSchema::Build(Actor->GetClass());
        SchemaPeakBytes = std::max(
            SchemaPeakBytes, Schema.GetStorageBytes());
        SchemaPeakReservedBytes = std::max(
            SchemaPeakReservedBytes, Schema.GetReservedStorageBytes());
        SchemaPeakFieldCount = std::max(
            SchemaPeakFieldCount, Schema.GetFields().size());
        if (Schema.GetClass() == nullptr) continue;
        if (Channel->State == EActorChannelState::PendingOpen)
        {
            if (QueueSpawn(*Channel, Actor, Schema, ObjectRegistry,
                    QueueReliable, bIsOwner))
                ++Statistics.SpawnMessagesSent;
        }
        else if (Channel->State == EActorChannelState::Open
            && QueueDelta(*Channel, Actor, Schema, ObjectRegistry,
                QueueReliable, bIsOwner))
        {
            ++Statistics.DeltaMessagesSent;
        }

        if (Channel->State == EActorChannelState::Open
            && QueueUnreliable && Actor->IsA(PCharacter::StaticClass()))
        {
            PCharacter* Character = static_cast<PCharacter*>(Actor);
            PCharacterMovementComponent* Movement =
                Character->GetCharacterMovement();
            if (Movement != nullptr)
            {
                FCharacterNetworkState State;
                State.ServerTick = static_cast<uint32>(World->GetTickCount());
                if (State.ServerTick == 0) State.ServerTick = 1;
                State.ServerTimeSeconds = World->GetTimeSeconds();
                State.LastProcessedMove =
                    Movement->GetLastProcessedNetworkMove();
                State.LastProcessedMoveClientTimeSeconds =
                    Movement->GetLastProcessedMoveClientTimeSeconds();
                State.State = Movement->CaptureMoveState();
                State.PolicyHash = Movement->GetNetworkPolicyHash();
                std::vector<uint8> Message;
                const EReplicationMessageType Type = bIsOwner
                    ? EReplicationMessageType::CharacterCorrection
                    : EReplicationMessageType::CharacterSnapshot;
                if (BuildCharacterStateMessage(Type, NetId, State, Message)
                    && QueueUnreliable(Message))
                {
                    if (bIsOwner) ++Statistics.CharacterCorrectionsSent;
                    else ++Statistics.CharacterSnapshotsSent;
                }
            }
        }
    }

    for (FImpl::FChannel& Channel : Impl->Channels)
    {
        if (Channel.ConnectionId != ConnectionId
            || Channel.State == EActorChannelState::Closed
            || Channel.State == EActorChannelState::PendingClose
            || Channel.PendingReliableId != 0) continue;
        PObject* Object = ResolveObject(Channel.ActorHandle);
        const bool bStillReplicated = Object != nullptr
            && Object->IsA(PActor::StaticClass())
            && static_cast<PActor*>(Object)->GetIsReplicated()
            && !static_cast<PActor*>(Object)->IsPendingDestroy();
        if (!bStillReplicated
            && QueueDestroy(Channel, QueueReliable))
        {
            ++Statistics.DestroyMessagesSent;
        }
    }
    MemoryTracker.Report(EMemoryTag::ReplicationSchema,
        SchemaPeakBytes, SchemaPeakReservedBytes, SchemaPeakFieldCount);
}

bool FReplicationSystem::HandleReliableMessage(
    FNetConnectionId ConnectionId,
    std::span<const uint8> Payload)
{
    return HandleMessage(ConnectionId, Payload, true);
}

bool FReplicationSystem::HandleUnreliableMessage(
    FNetConnectionId ConnectionId,
    std::span<const uint8> Payload)
{
    return HandleMessage(ConnectionId, Payload, false);
}

bool FReplicationSystem::HandleMessage(
    FNetConnectionId ConnectionId,
    std::span<const uint8> Payload,
    bool bReliable)
{
    if (World == nullptr || !ConnectionId.IsValid()) return false;
    if (Impl == nullptr) Impl = std::make_shared<FImpl>();
    FNetByteReader Reader(Payload);
    uint16 Magic = 0;
    uint8 Version = 0;
    uint8 RawType = 0;
    if (!Reader.ReadUInt16(Magic) || Magic != ReplicationMagic
        || !Reader.ReadUInt8(Version) || Version != ReplicationVersion
        || !Reader.ReadUInt8(RawType)) return false;

    const auto Reject = [this]()
    {
        ++Statistics.RejectedMessages;
        return false;
    };
    const EReplicationMessageType Type =
        static_cast<EReplicationMessageType>(RawType);
    uint32 RawNetId = 0;
    if (!Reader.ReadUInt32(RawNetId) || RawNetId == 0) return Reject();
    const FNetObjectId NetId {RawNetId};

    if (Type == EReplicationMessageType::CharacterMove)
    {
        const auto RejectMove = [this, &Reject]()
        {
            ++Statistics.CharacterMoveMessagesRejected;
            return Reject();
        };
        if (bReliable) return RejectMove();
        PActor* Target = ObjectRegistry.ResolveActor(NetId);
        if (Target == nullptr || !Target->IsA(PCharacter::StaticClass())
            || Target->GetLocalRole() != ENetRole::Authority
            || GetActorOwningConnection(Target) != ConnectionId)
            return RejectMove();
        uint8 Count = 0;
        uint64 PolicyHash = 0;
        if (!Reader.ReadUInt8(Count) || Count == 0 || Count > 3
            || !Reader.ReadUInt64(PolicyHash))
            return RejectMove();
        std::vector<FCharacterNetworkMove> Moves;
        Moves.reserve(Count);
        for (uint8 Index = 0; Index < Count; ++Index)
        {
            FCharacterNetworkMove Move;
            uint8 Jump = 0;
            Move.PolicyHash = PolicyHash;
            if (!Reader.ReadUInt32(Move.Sequence)
                || !ReadDouble(Reader, Move.ClientTimeSeconds)
                || !ReadFloat(Reader, Move.DeltaSeconds)
                || !ReadVector(Reader, Move.Input.WorldInput)
                || !Reader.ReadUInt8(Jump) || Jump > 1
                || !ReadFloat(Reader, Move.ControlYaw))
                return RejectMove();
            Move.Input.bJumpPressed = Jump != 0;
            Move.Input.RootMotionDelta = FTransform::Identity;
            Moves.push_back(Move);
        }
        if (Reader.GetRemainingBytes() != 0) return RejectMove();
        PCharacterMovementComponent* Movement =
            static_cast<PCharacter*>(Target)->GetCharacterMovement();
        if (Movement == nullptr) return RejectMove();
        for (const FCharacterNetworkMove& Move : Moves)
        {
            if (!Movement->EnqueueServerMove(Move)) return RejectMove();
        }
        ++Statistics.CharacterMoveMessagesReceived;
        ++Statistics.MessagesReceived;
        return true;
    }

    if (Type == EReplicationMessageType::CharacterCorrection
        || Type == EReplicationMessageType::CharacterSnapshot)
    {
        if (bReliable) return Reject();
        PActor* Target = ObjectRegistry.ResolveActor(NetId);
        if (Target == nullptr || !Target->IsA(PCharacter::StaticClass()))
            return Reject();
        FCharacterNetworkState State;
        if (!ReadCharacterState(Reader, State)
            || Reader.GetRemainingBytes() != 0)
            return Reject();
        PCharacterMovementComponent* Movement =
            static_cast<PCharacter*>(Target)->GetCharacterMovement();
        if (Movement == nullptr) return Reject();
        if (Type == EReplicationMessageType::CharacterCorrection)
        {
            if (Target->GetLocalRole() != ENetRole::AutonomousProxy)
                return Reject();
            Movement->ReceiveNetworkCorrection(State);
            ++Statistics.CharacterCorrectionsReceived;
        }
        else
        {
            if (Target->GetLocalRole() != ENetRole::SimulatedProxy)
                return Reject();
            Movement->ReceiveSimulatedSnapshot(State);
            ++Statistics.CharacterSnapshotsReceived;
        }
        ++Statistics.MessagesReceived;
        return true;
    }

    if (Type == EReplicationMessageType::Rpc)
    {
        const auto RejectRpc = [this, &Reject]()
        {
            ++Statistics.RpcMessagesRejected;
            return Reject();
        };
        auto CountIt = std::find_if(
            Impl->RpcFrameCounts.begin(), Impl->RpcFrameCounts.end(),
            [ConnectionId](const FImpl::FRpcFrameCount& Count)
            {
                return Count.ConnectionId == ConnectionId;
            });
        if (CountIt == Impl->RpcFrameCounts.end())
        {
            Impl->RpcFrameCounts.push_back({ConnectionId, 0});
            CountIt = std::prev(Impl->RpcFrameCounts.end());
        }
        if (CountIt->Count >= MaxRpcCallsPerConnectionPerFrame)
            return RejectRpc();
        ++CountIt->Count;
        PActor* Target = ObjectRegistry.ResolveActor(NetId);
        std::string FunctionName;
        uint8 ArgumentCount = 0;
        if (Target == nullptr
            || !ReadString(Reader, FunctionName)
            || !Reader.ReadUInt8(ArgumentCount)
            || ArgumentCount > MaxRpcArguments) return RejectRpc();
        const PFunction* Function = Target->GetClass()->FindFunction(
            FName(FunctionName));
        if (Function == nullptr
            || Function->GetReturnValue().Type != EFunctionValueType::Void
            || Function->GetParameters().size() != ArgumentCount)
            return RejectRpc();
        const EFunctionFlags Flags = Function->GetFlags();
        const bool bServerFunction = HasAnyFlags(Flags, EFunctionFlags::Server);
        const bool bClientFunction = HasAnyFlags(Flags, EFunctionFlags::Client);
        const bool bMulticastFunction =
            HasAnyFlags(Flags, EFunctionFlags::NetMulticast);
        if (static_cast<int>(bServerFunction)
                + static_cast<int>(bClientFunction)
                + static_cast<int>(bMulticastFunction) != 1
            || HasAnyFlags(Flags, EFunctionFlags::Reliable) != bReliable)
            return RejectRpc();
        if (Target->GetLocalRole() == ENetRole::Authority)
        {
            if (!bServerFunction
                || GetActorOwningConnection(Target) != ConnectionId)
                return RejectRpc();
        }
        else if (!bClientFunction && !bMulticastFunction)
        {
            return RejectRpc();
        }

        std::vector<FFunctionValue> Arguments;
        Arguments.reserve(ArgumentCount);
        for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
        {
            uint16 ValueSize = 0;
            std::vector<uint8> Data;
            if (!Reader.ReadUInt16(ValueSize)
                || !Reader.ReadBytes(ValueSize, Data)) return RejectRpc();
            FFunctionValue Value;
            if (!DecodeRpcArgument(
                    Data,
                    Function->GetParameters()[Index].Value,
                    ObjectRegistry,
                    Value)) return RejectRpc();
            Arguments.push_back(std::move(Value));
        }
        if (Reader.GetRemainingBytes() != 0
            || Target->ProcessEvent(Function, Arguments)
                != EFunctionInvokeResult::Success) return RejectRpc();
        ++Statistics.RpcMessagesReceived;
        ++Statistics.MessagesReceived;
        return true;
    }

    if (Type == EReplicationMessageType::Destroy)
    {
        if (Reader.GetRemainingBytes() != 0) return Reject();
        if (PActor* Actor = ObjectRegistry.ResolveActor(NetId))
        {
            World->DestroyActor(Actor);
        }
        ObjectRegistry.RemoveByNetId(NetId);
        std::erase_if(Impl->Channels,
            [ConnectionId, NetId](const FImpl::FChannel& Channel)
            {
                return Channel.ConnectionId == ConnectionId
                    && Channel.NetObjectId == NetId;
            });
        std::erase_if(Impl->PendingReferences,
            [NetId](const FPendingReference& Pending)
            {
                return Pending.TargetNetId == NetId;
            });
        ++Statistics.MessagesReceived;
        return true;
    }

    PActor* Actor = nullptr;
    FReplicationSchema Schema;
    std::vector<uint8> TransformData;
    std::vector<FFieldValue> Values;
    if (Type == EReplicationMessageType::Spawn)
    {
        std::string ClassName;
        std::string ActorName;
        uint8 OwnedByConnection = 0;
        uint32 SchemaHash = 0;
        uint16 TransformSize = 0;
        if (!ReadString(Reader, ClassName)
            || !ReadString(Reader, ActorName)
            || !Reader.ReadUInt8(OwnedByConnection)
            || OwnedByConnection > 1
            || !Reader.ReadUInt32(SchemaHash)
            || !Reader.ReadUInt16(TransformSize)
            || !Reader.ReadBytes(TransformSize, TransformData)
            || !ReadFieldValues(Reader, Values)
            || Reader.GetRemainingBytes() != 0) return Reject();
        const PClass* Class = FClassRegistry::FindClass(FName(ClassName));
        if (Class == nullptr || !Class->IsChildOf(PActor::StaticClass())) return Reject();
        Schema = FReplicationSchema::Build(Class);
        FTransform ValidatedTransform;
        if (Schema.GetHash() != SchemaHash
            || (!TransformData.empty()
                && !DecodeTransform(TransformData, ValidatedTransform))
            || !ValidateIncomingValues(Schema, Values, ObjectRegistry))
            return Reject();
        std::size_t NewPendingReferences = 0;
        for (const FFieldValue& Value : Values)
        {
            if (Value.Type != EPropertyType::Object) continue;
            FNetObjectId TargetId;
            if (!ReadObjectNetId(Value.Data, TargetId)) return Reject();
            if (TargetId.IsValid()
                && ObjectRegistry.ResolveActor(TargetId) == nullptr)
                ++NewPendingReferences;
        }
        if (Impl->PendingReferences.size() + NewPendingReferences
            > MaxPendingObjectReferences) return Reject();

        Actor = ObjectRegistry.ResolveActor(NetId);
        if (Actor != nullptr && Actor->GetClass() != Class) return Reject();
        if (Actor == nullptr)
        {
            Actor = FindUnboundStartupActor(World, Class, FName(ActorName));
            if (Actor == nullptr)
            {
                Actor = World->SpawnActor(Class, FName(ActorName));
            }
            if (Actor == nullptr
                || !ObjectRegistry.RegisterRemoteObject(NetId, Actor)) return Reject();
            Actor->SetReplicates(true);
        }
        Actor->SetNetRoles(
            OwnedByConnection != 0
                ? ENetRole::AutonomousProxy
                : ENetRole::SimulatedProxy,
            ENetRole::Authority);
        if (Actor->IsA(PGameStateBase::StaticClass()))
        {
            World->BindReplicatedGameState(
                static_cast<PGameStateBase*>(Actor));
        }
        if (FindChannel(*Impl, ConnectionId, NetId) == nullptr)
        {
            FImpl::FChannel Channel;
            Channel.ConnectionId = ConnectionId;
            Channel.NetObjectId = NetId;
            Channel.ActorHandle = Actor->GetHandle();
            Channel.State = EActorChannelState::Open;
            Impl->Channels.push_back(std::move(Channel));
        }
    }
    else if (Type == EReplicationMessageType::Delta)
    {
        uint32 SchemaHash = 0;
        uint8 HasTransform = 0;
        if (!Reader.ReadUInt32(SchemaHash)
            || !Reader.ReadUInt8(HasTransform) || HasTransform > 1) return Reject();
        if (HasTransform != 0)
        {
            uint16 TransformSize = 0;
            if (!Reader.ReadUInt16(TransformSize)
                || !Reader.ReadBytes(TransformSize, TransformData)) return Reject();
        }
        if (!ReadFieldValues(Reader, Values)
            || Reader.GetRemainingBytes() != 0) return Reject();
        Actor = ObjectRegistry.ResolveActor(NetId);
        if (Actor == nullptr) return Reject();
        Schema = FReplicationSchema::Build(Actor->GetClass());
        FTransform ValidatedTransform;
        if (Schema.GetHash() != SchemaHash
            || (!TransformData.empty()
                && !DecodeTransform(TransformData, ValidatedTransform))
            || !ValidateIncomingValues(Schema, Values, ObjectRegistry))
            return Reject();
        std::size_t NewPendingReferences = 0;
        for (const FFieldValue& Value : Values)
        {
            if (Value.Type != EPropertyType::Object) continue;
            FNetObjectId TargetId;
            if (!ReadObjectNetId(Value.Data, TargetId)) return Reject();
            if (TargetId.IsValid()
                && ObjectRegistry.ResolveActor(TargetId) == nullptr)
                ++NewPendingReferences;
        }
        if (Impl->PendingReferences.size() + NewPendingReferences
            > MaxPendingObjectReferences) return Reject();
    }
    else
    {
        return Reject();
    }

    if (!TransformData.empty()
        && !(Actor->IsA(PCharacter::StaticClass())
            && (Actor->GetLocalRole() == ENetRole::AutonomousProxy
                || Actor->GetLocalRole() == ENetRole::SimulatedProxy)))
    {
        FTransform Transform;
        if (!DecodeTransform(TransformData, Transform)
            || !Actor->SetActorTransform(Transform)) return Reject();
    }

    std::vector<const FReplicationFieldDescriptor*> RepNotifies;
    for (const FFieldValue& Value : Values)
    {
        const FReplicationFieldDescriptor* Field = Schema.FindField(Value.FieldId);
        if (Field == nullptr || Field->Property == nullptr
            || Field->Property->GetType() != Value.Type) return Reject();
        std::vector<uint8> Previous;
        if (!EncodePropertyValue(
                *Field->Property, Actor, ObjectRegistry, Previous)) return Reject();
        if (Previous == Value.Data) continue;
        if (Value.Type == EPropertyType::Object)
        {
            FNetObjectId TargetId;
            if (!ReadObjectNetId(Value.Data, TargetId)) return Reject();
            PActor* Target = TargetId.IsValid()
                ? ObjectRegistry.ResolveActor(TargetId) : nullptr;
            std::erase_if(Impl->PendingReferences,
                [Actor, Field](const FPendingReference& Pending)
                {
                    return Pending.OwnerHandle == Actor->GetHandle()
                        && Pending.Property == Field->Property;
                });
            if (!Field->Property->SetReferencedObjectSilently(Actor, Target))
                return Reject();
            if (TargetId.IsValid() && Target == nullptr)
            {
                if (Impl->PendingReferences.size()
                    >= MaxPendingObjectReferences) return Reject();
                Impl->PendingReferences.push_back(
                    {Actor->GetHandle(), Field->Property, TargetId,
                        Field->RepNotifyFunction});
                continue;
            }
        }
        else if (!ApplyNonObjectProperty(
            *Field->Property, Actor, Value.Data)) return Reject();
        if (!Field->RepNotifyFunction.IsNone()) RepNotifies.push_back(Field);
    }

    for (const FReplicationFieldDescriptor* Field : RepNotifies)
    {
        const PFunction* Function = Actor->GetClass()->FindFunction(
            Field->RepNotifyFunction);
        if (Function != nullptr && Function->GetParameters().empty()
            && Actor->ProcessEvent(Function) == EFunctionInvokeResult::Success)
        {
            ++Statistics.OnRepCalls;
        }
    }

    for (auto It = Impl->PendingReferences.begin();
        It != Impl->PendingReferences.end();)
    {
        PObject* Owner = ResolveObject(It->OwnerHandle);
        PActor* Target = ObjectRegistry.ResolveActor(It->TargetNetId);
        if (Owner == nullptr || It->Property == nullptr)
        {
            It = Impl->PendingReferences.erase(It);
        }
        else if (Target != nullptr)
        {
            const bool bSet =
                It->Property->SetReferencedObjectSilently(Owner, Target);
            if (bSet && !It->RepNotifyFunction.IsNone())
            {
                const PFunction* Function = Owner->GetClass()->FindFunction(
                    It->RepNotifyFunction);
                if (Function != nullptr && Function->GetParameters().empty()
                    && Owner->ProcessEvent(Function)
                        == EFunctionInvokeResult::Success)
                {
                    ++Statistics.OnRepCalls;
                }
            }
            It = Impl->PendingReferences.erase(It);
        }
        else
        {
            ++It;
        }
    }
    ++Statistics.MessagesReceived;
    return true;
}

bool FReplicationSystem::BuildCharacterMoveMessage(
    PActor* Target,
    std::span<const FCharacterNetworkMove> Moves,
    std::vector<uint8>& OutMessage)
{
    OutMessage.clear();
    if (Target == nullptr || !Target->IsA(PCharacter::StaticClass())
        || Moves.empty() || Moves.size() > 3)
        return false;
    const FNetObjectId NetId = ObjectRegistry.FindNetId(Target->GetHandle());
    if (!NetId.IsValid()) return false;
    const uint64 PolicyHash = Moves.front().PolicyHash;
    FNetByteWriter Writer;
    if (!WriteMessageHeader(Writer, EReplicationMessageType::CharacterMove)
        || !Writer.WriteUInt32(NetId.Value)
        || !Writer.WriteUInt8(static_cast<uint8>(Moves.size()))
        || !Writer.WriteUInt64(PolicyHash))
        return false;
    for (const FCharacterNetworkMove& Move : Moves)
    {
        if (Move.Sequence == 0 || Move.PolicyHash != PolicyHash
            || !std::isfinite(Move.ClientTimeSeconds)
            || Move.ClientTimeSeconds < 0.0
            || !std::isfinite(Move.DeltaSeconds)
            || Move.DeltaSeconds <= 0.0f || Move.DeltaSeconds > 0.125f
            || !std::isfinite(Move.Input.WorldInput.X)
            || !std::isfinite(Move.Input.WorldInput.Y)
            || !std::isfinite(Move.Input.WorldInput.Z)
            || Move.Input.WorldInput.SizeSquared() > 1.21f
            || !std::isfinite(Move.ControlYaw)
            || !Writer.WriteUInt32(Move.Sequence)
            || !WriteDouble(Writer, Move.ClientTimeSeconds)
            || !WriteFloat(Writer, Move.DeltaSeconds)
            || !WriteVector(Writer, Move.Input.WorldInput)
            || !Writer.WriteUInt8(Move.Input.bJumpPressed ? 1 : 0)
            || !WriteFloat(Writer, Move.ControlYaw))
            return false;
    }
    OutMessage = Writer.GetBytes();
    return true;
}

void FReplicationSystem::RecordCharacterMovesSent(std::size_t)
{
    ++Statistics.CharacterMoveMessagesSent;
}

bool FReplicationSystem::BuildRpcMessage(
    PActor* Target,
    FName FunctionName,
    std::span<const FFunctionValue> Arguments,
    std::vector<uint8>& OutMessage)
{
    OutMessage.clear();
    if (Target == nullptr || FunctionName.IsNone()
        || Arguments.size() > MaxRpcArguments) return false;
    const PFunction* Function = Target->GetClass()->FindFunction(FunctionName);
    if (Function == nullptr
        || Function->GetReturnValue().Type != EFunctionValueType::Void
        || Function->GetParameters().size() != Arguments.size()) return false;
    const EFunctionFlags Flags = Function->GetFlags();
    const int DirectionCount =
        static_cast<int>(HasAnyFlags(Flags, EFunctionFlags::Server))
        + static_cast<int>(HasAnyFlags(Flags, EFunctionFlags::Client))
        + static_cast<int>(HasAnyFlags(Flags, EFunctionFlags::NetMulticast));
    if (DirectionCount != 1) return false;

    FNetObjectId NetId = ObjectRegistry.FindNetId(Target->GetHandle());
    if (!NetId.IsValid() && Target->GetLocalRole() == ENetRole::Authority)
        NetId = ObjectRegistry.RegisterAuthorityObject(Target);
    if (!NetId.IsValid()) return false;

    FNetByteWriter Writer;
    if (!WriteMessageHeader(Writer, EReplicationMessageType::Rpc)
        || !Writer.WriteUInt32(NetId.Value)
        || !WriteString(Writer, FunctionName.ToString())
        || !Writer.WriteUInt8(static_cast<uint8>(Arguments.size()))) return false;
    for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
    {
        std::vector<uint8> Data;
        if (!EncodeRpcArgument(
                Arguments[Index],
                Function->GetParameters()[Index].Value,
                ObjectRegistry,
                Data)
            || Data.size() > std::numeric_limits<uint16>::max()
            || !Writer.WriteUInt16(static_cast<uint16>(Data.size()))
            || !Writer.WriteBytes(Data)) return false;
    }
    OutMessage = Writer.GetBytes();
    return true;
}

void FReplicationSystem::RecordRpcSent()
{
    ++Statistics.RpcMessagesSent;
}

void FReplicationSystem::HandleReliableAcknowledged(
    FNetConnectionId ConnectionId,
    uint32 ReliableId)
{
    if (Impl == nullptr || ReliableId == 0) return;
    FImpl::FChannel* Channel = nullptr;
    for (FImpl::FChannel& Candidate : Impl->Channels)
    {
        if (Candidate.ConnectionId == ConnectionId
            && Candidate.PendingReliableId == ReliableId)
        {
            Channel = &Candidate;
            break;
        }
    }
    if (Channel == nullptr) return;
    if (Channel->PendingType == EReplicationMessageType::Destroy)
    {
        Channel->State = EActorChannelState::Closed;
    }
    else
    {
        MergeBaseline(Channel->Baseline, Channel->PendingValues);
        if (!Channel->PendingTransform.empty())
            Channel->TransformBaseline = Channel->PendingTransform;
        Channel->State = EActorChannelState::Open;
    }
    Channel->PendingReliableId = 0;
    Channel->PendingValues.clear();
    Channel->PendingTransform.clear();

    const FNetObjectId ClosedNetId = Channel->NetObjectId;
    std::erase_if(Impl->Channels,
        [](const FImpl::FChannel& Candidate)
        {
            return Candidate.State == EActorChannelState::Closed;
        });
    const bool bStillHasChannel = std::any_of(
        Impl->Channels.begin(), Impl->Channels.end(),
        [ClosedNetId](const FImpl::FChannel& Candidate)
        {
            return Candidate.NetObjectId == ClosedNetId;
        });
    if (!bStillHasChannel
        && ObjectRegistry.ResolveActor(ClosedNetId) == nullptr)
    {
        ObjectRegistry.RemoveByNetId(ClosedNetId);
    }
}

void FReplicationSystem::HandleConnectionClosed(
    FNetConnectionId ConnectionId)
{
    if (Impl == nullptr) return;
    std::vector<FNetObjectId> CandidateIds;
    for (const FImpl::FChannel& Channel : Impl->Channels)
    {
        if (Channel.ConnectionId == ConnectionId)
            CandidateIds.push_back(Channel.NetObjectId);
    }
    std::erase_if(Impl->Channels,
        [ConnectionId](const FImpl::FChannel& Channel)
        {
            return Channel.ConnectionId == ConnectionId;
        });
    std::erase_if(Impl->Ownership,
        [ConnectionId](const FImpl::FOwnership& Ownership)
        {
            return Ownership.ConnectionId == ConnectionId;
        });
    for (FNetObjectId NetId : CandidateIds)
    {
        const bool bStillUsed = std::any_of(
            Impl->Channels.begin(), Impl->Channels.end(),
            [NetId](const FImpl::FChannel& Channel)
            {
                return Channel.NetObjectId == NetId;
            });
        if (!bStillUsed && ObjectRegistry.ResolveActor(NetId) == nullptr)
            ObjectRegistry.RemoveByNetId(NetId);
    }
}

void FReplicationSystem::SetActorOwningConnection(
    PActor* Actor, FNetConnectionId ConnectionId)
{
    if (Actor == nullptr) return;
    if (Impl == nullptr) Impl = std::make_shared<FImpl>();
    std::erase_if(Impl->Ownership,
        [Actor](const FImpl::FOwnership& Ownership)
        {
            return Ownership.ActorHandle == Actor->GetHandle();
        });
    if (ConnectionId.IsValid())
    {
        Impl->Ownership.push_back({Actor->GetHandle(), ConnectionId});
    }
}

FNetConnectionId FReplicationSystem::GetActorOwningConnection(
    const PActor* Actor) const
{
    if (Actor == nullptr || Impl == nullptr) return {};
    for (const PActor* Current = Actor; Current != nullptr;
        Current = Current->GetOwner())
    {
        const auto It = std::find_if(
            Impl->Ownership.begin(), Impl->Ownership.end(),
            [Current](const FImpl::FOwnership& Ownership)
            {
                return Ownership.ActorHandle == Current->GetHandle();
            });
        if (It != Impl->Ownership.end()) return It->ConnectionId;
    }
    return {};
}

std::vector<FActorChannelSnapshot>
FReplicationSystem::GetChannelSnapshots() const
{
    std::vector<FActorChannelSnapshot> Result;
    if (Impl == nullptr) return Result;
    Result.reserve(Impl->Channels.size());
    for (const FImpl::FChannel& Channel : Impl->Channels)
    {
        Result.push_back({
            Channel.ConnectionId,
            Channel.NetObjectId,
            Channel.ActorHandle,
            Channel.State,
            Channel.Baseline.size(),
            Channel.PendingReliableId});
    }
    return Result;
}

FReplicationStatistics FReplicationSystem::GetStatistics() const
{
    FReplicationStatistics Result = Statistics;
    Result.ChannelCount = Impl != nullptr ? Impl->Channels.size() : 0;
    Result.NetObjectCount = ObjectRegistry.Num();
    Result.UnresolvedReferenceCount =
        Impl != nullptr ? Impl->PendingReferences.size() : 0;
    return Result;
}

void FReplicationSystem::PublishMemoryStatistics() const
{
    FMemoryTracker& Tracker = FMemoryTracker::Get();
    if (!Tracker.IsEnabled()) return;
    if (Impl == nullptr)
    {
        Tracker.Report(EMemoryTag::ReplicationChannels, 0, 0, 0);
        return;
    }

    std::size_t CurrentBytes = Impl->Channels.size()
        * sizeof(FImpl::FChannel)
        + Impl->PendingReferences.size() * sizeof(FPendingReference)
        + Impl->Ownership.size() * sizeof(FImpl::FOwnership)
        + Impl->RpcFrameCounts.size() * sizeof(FImpl::FRpcFrameCount);
    std::size_t ReservedBytes = Impl->Channels.capacity()
        * sizeof(FImpl::FChannel)
        + Impl->PendingReferences.capacity() * sizeof(FPendingReference)
        + Impl->Ownership.capacity() * sizeof(FImpl::FOwnership)
        + Impl->RpcFrameCounts.capacity() * sizeof(FImpl::FRpcFrameCount);
    for (const FImpl::FChannel& Channel : Impl->Channels)
    {
        CurrentBytes += Channel.Baseline.size() * sizeof(FFieldValue)
            + Channel.TransformBaseline.size()
            + Channel.PendingValues.size() * sizeof(FFieldValue)
            + Channel.PendingTransform.size();
        ReservedBytes += Channel.Baseline.capacity() * sizeof(FFieldValue)
            + Channel.TransformBaseline.capacity()
            + Channel.PendingValues.capacity() * sizeof(FFieldValue)
            + Channel.PendingTransform.capacity();
        for (const FFieldValue& Value : Channel.Baseline)
        {
            CurrentBytes += Value.Data.size();
            ReservedBytes += Value.Data.capacity();
        }
        for (const FFieldValue& Value : Channel.PendingValues)
        {
            CurrentBytes += Value.Data.size();
            ReservedBytes += Value.Data.capacity();
        }
    }
    Tracker.Report(EMemoryTag::ReplicationChannels,
        CurrentBytes, ReservedBytes, Impl->Channels.size());
}
}
