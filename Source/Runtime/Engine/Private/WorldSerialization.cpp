#include "Pico/Engine/WorldSerialization.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/SerializationFile.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
constexpr uint32 WorldMagic = 0x444c5750;
constexpr uint32 WorldFormatVersion = 1;
constexpr uint32 MaxSceneObjectCount = 64 * 1024;
constexpr uint32 MaxSceneRelationCount = 128 * 1024;
constexpr uint32 MaxObjectPropertyCount = 4 * 1024;
constexpr std::size_t MaxSceneNameLength = 1024;
constexpr std::size_t MaxWorldFileSize = 256 * 1024 * 1024;

void ReportError(EWorldSerializationError* OutError, EWorldSerializationError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}

const FSceneObjectRecord* FindRecord(
    const std::unordered_map<uint64, const FSceneObjectRecord*>& Records,
    FSceneObjectId Id)
{
    const auto Found = Records.find(Id.Value);
    return Found != Records.end() ? Found->second : nullptr;
}

const PClass* FindRecordClass(const FSceneObjectRecord& Record)
{
    return FClassRegistry::FindClass(FName(Record.ClassName));
}

bool IsChildOf(const FSceneObjectRecord& Record, const PClass* BaseClass)
{
    const PClass* Class = FindRecordClass(Record);
    return Class != nullptr && Class->IsChildOf(BaseClass);
}

bool IsObjectNameUnique(
    const FSceneObjectRecord& Record,
    std::unordered_set<std::string>& Names)
{
    std::string Key = std::to_string(Record.OuterId.Value);
    Key.push_back('\0');
    Key.append(Record.ObjectName);
    return Names.insert(std::move(Key)).second;
}

bool HasOuterCycle(
    const FSceneObjectRecord& Start,
    const std::unordered_map<uint64, const FSceneObjectRecord*>& Records)
{
    std::unordered_set<uint64> Visited;
    const FSceneObjectRecord* Current = &Start;
    while (Current != nullptr && Current->OuterId.IsValid())
    {
        if (!Visited.insert(Current->Id.Value).second)
        {
            return true;
        }
        Current = FindRecord(Records, Current->OuterId);
    }
    return Current == nullptr;
}

bool HasAttachmentCycle(
    uint64 StartId,
    const std::unordered_map<uint64, uint64>& AttachParents)
{
    std::unordered_set<uint64> Visited;
    uint64 CurrentId = StartId;
    while (CurrentId != 0)
    {
        if (!Visited.insert(CurrentId).second)
        {
            return true;
        }
        const auto Found = AttachParents.find(CurrentId);
        CurrentId = Found != AttachParents.end() ? Found->second : 0;
    }
    return false;
}

bool AddObjectRecord(
    const PObject& Object,
    FSceneObjectId OuterId,
    FWorldAssetData& Data,
    std::unordered_map<const PObject*, FSceneObjectId>& ObjectIds,
    EWorldSerializationError* OutError)
{
    if (Data.Objects.size() >= MaxSceneObjectCount
        || Object.GetClass() == nullptr
        || Object.GetName().IsNone())
    {
        ReportError(
            OutError,
            Data.Objects.size() >= MaxSceneObjectCount
                ? EWorldSerializationError::ObjectLimitExceeded
                : EWorldSerializationError::InvalidObjectGraph);
        return false;
    }

    const FSceneObjectId Id { static_cast<uint64>(Data.Objects.size()) + 1 };
    if (!ObjectIds.emplace(&Object, Id).second)
    {
        ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
        return false;
    }

    FSceneObjectRecord Record;
    Record.Id = Id;
    Record.OuterId = OuterId;
    Record.ClassName = Object.GetClass()->GetName().ToString();
    Record.ObjectName = Object.GetName().ToString();
    Record.Flags = Object.GetFlags();
    if (!CaptureSerializedProperties(&Object, Record.Properties))
    {
        ReportError(OutError, EWorldSerializationError::PropertyAccessFailed);
        return false;
    }
    if (Record.Properties.size() > MaxObjectPropertyCount)
    {
        ReportError(OutError, EWorldSerializationError::PropertyLimitExceeded);
        return false;
    }

    Data.Objects.push_back(std::move(Record));
    return true;
}

bool SerializeObjectRecord(FArchive& Archive, FSceneObjectRecord& Record)
{
    Archive.SerializeUInt64(Record.Id.Value);
    Archive.SerializeUInt64(Record.OuterId.Value);
    Archive.SerializeString(Record.ClassName);
    Archive.SerializeString(Record.ObjectName);

    uint32 Flags = Archive.IsSaving() ? static_cast<uint32>(Record.Flags) : 0;
    Archive.SerializeUInt32(Flags);
    if (Archive.IsLoading())
    {
        Record.Flags = static_cast<EObjectFlags>(Flags);
    }

    uint32 PropertyCount = Archive.IsSaving()
        ? static_cast<uint32>(Record.Properties.size())
        : 0;
    Archive.SerializeUInt32(PropertyCount);
    if (Archive.HasError() || PropertyCount > MaxObjectPropertyCount)
    {
        return false;
    }
    if (Archive.IsLoading())
    {
        Record.Properties.clear();
        Record.Properties.resize(PropertyCount);
    }

    for (FSerializedPropertyRecord& Property : Record.Properties)
    {
        if (!SerializePropertyRecord(Archive, Property))
        {
            return false;
        }
    }
    return !Archive.HasError();
}

void SerializeRelationRecord(FArchive& Archive, FSceneRelationRecord& Relation)
{
    Archive.SerializeUInt64(Relation.ObjectId.Value);
    Archive.SerializeUInt64(Relation.RootComponentId.Value);
    Archive.SerializeUInt64(Relation.AttachParentId.Value);
}
}

class FWorldAssetLoader
{
public:
    static PWorld* Create(
        const FWorldAssetData& Data,
        EWorldSerializationError* OutError)
    {
        if (!ValidateWorldAssetData(Data, OutError))
        {
            return nullptr;
        }

        std::unordered_map<uint64, const FSceneObjectRecord*> Records;
        Records.reserve(Data.Objects.size());
        for (const FSceneObjectRecord& Record : Data.Objects)
        {
            Records.emplace(Record.Id.Value, &Record);
        }

        const FSceneObjectRecord* WorldRecord = FindRecord(Records, Data.WorldId);
        PWorld* World = nullptr;
        try
        {
            World = static_cast<PWorld*>(NewObject(
                FindRecordClass(*WorldRecord),
                nullptr,
                FName(WorldRecord->ObjectName),
                WorldRecord->Flags));
        }
        catch (...)
        {
            ReportError(OutError, EWorldSerializationError::ObjectCreationFailed);
            return nullptr;
        }
        if (World == nullptr)
        {
            ReportError(OutError, EWorldSerializationError::ObjectCreationFailed);
            return nullptr;
        }

        const auto Fail =
            [World, OutError](EWorldSerializationError Error) -> PWorld*
            {
                ReportError(OutError, Error);
                DestroyObjectTree(World);
                return nullptr;
            };

        std::unordered_map<uint64, PObject*> Objects;
        Objects.reserve(Data.Objects.size());
        Objects.emplace(Data.WorldId.Value, World);

        try
        {
            for (const FSceneObjectRecord& Record : Data.Objects)
            {
                const PClass* Class = FindRecordClass(Record);
                if (Record.Id == Data.WorldId
                    || !Class->IsChildOf(PLevel::StaticClass()))
                {
                    continue;
                }

                PLevel* Level = static_cast<PLevel*>(NewObject(
                    Class,
                    World,
                    FName(Record.ObjectName),
                    Record.Flags));
                if (Level == nullptr)
                {
                    return Fail(EWorldSerializationError::ObjectCreationFailed);
                }
                World->LevelHandles.push_back(Level->GetHandle());
                Objects.emplace(Record.Id.Value, Level);
            }

            PObject* PersistentObject = Objects.at(Data.PersistentLevelId.Value);
            PObject* CurrentObject = Objects.at(Data.CurrentLevelId.Value);
            World->PersistentLevelHandle = PersistentObject->GetHandle();
            World->CurrentLevelHandle = CurrentObject->GetHandle();
            World->State = EWorldState::Initialized;

            for (const FSceneObjectRecord& Record : Data.Objects)
            {
                const PClass* Class = FindRecordClass(Record);
                if (!Class->IsChildOf(PActor::StaticClass()))
                {
                    continue;
                }

                PLevel* Level =
                    static_cast<PLevel*>(Objects.at(Record.OuterId.Value));
                PActor* Actor = static_cast<PActor*>(NewObject(
                    Class,
                    Level,
                    FName(Record.ObjectName),
                    Record.Flags));
                if (Actor == nullptr)
                {
                    return Fail(EWorldSerializationError::ObjectCreationFailed);
                }
                Level->AddActor(Actor);
                Objects.emplace(Record.Id.Value, Actor);
            }

            for (const FSceneObjectRecord& Record : Data.Objects)
            {
                const PClass* Class = FindRecordClass(Record);
                if (!Class->IsChildOf(PActorComponent::StaticClass()))
                {
                    continue;
                }

                PActor* Actor =
                    static_cast<PActor*>(Objects.at(Record.OuterId.Value));
                PActorComponent* Component =
                    static_cast<PActorComponent*>(NewObject(
                        Class,
                        Actor,
                        FName(Record.ObjectName),
                        Record.Flags));
                if (Component == nullptr)
                {
                    return Fail(EWorldSerializationError::ObjectCreationFailed);
                }
                Actor->ComponentHandles.push_back(Component->GetHandle());
                Objects.emplace(Record.Id.Value, Component);
            }
        }
        catch (...)
        {
            return Fail(EWorldSerializationError::ObjectCreationFailed);
        }

        for (const FSceneObjectRecord& Record : Data.Objects)
        {
            PObject* Object = Objects.at(Record.Id.Value);
            for (const FSerializedPropertyRecord& Property : Record.Properties)
            {
                const ESerializedPropertyApplyResult Result =
                    ApplySerializedProperty(Object, Property);
                if (Result == ESerializedPropertyApplyResult::TypeMismatch)
                {
                    return Fail(EWorldSerializationError::PropertyTypeMismatch);
                }
                if (Result != ESerializedPropertyApplyResult::None)
                {
                    return Fail(EWorldSerializationError::PropertyAccessFailed);
                }
            }
        }

        for (const FSceneRelationRecord& Relation : Data.Relations)
        {
            if (!Relation.RootComponentId.IsValid())
            {
                continue;
            }

            PActor* Actor =
                static_cast<PActor*>(Objects.at(Relation.ObjectId.Value));
            PSceneComponent* Root = static_cast<PSceneComponent*>(
                Objects.at(Relation.RootComponentId.Value));
            if (!Actor->SetRootComponent(Root))
            {
                return Fail(EWorldSerializationError::RelationRestoreFailed);
            }
        }

        for (const FSceneRelationRecord& Relation : Data.Relations)
        {
            if (!Relation.AttachParentId.IsValid())
            {
                continue;
            }

            PSceneComponent* Component = static_cast<PSceneComponent*>(
                Objects.at(Relation.ObjectId.Value));
            PSceneComponent* Parent = static_cast<PSceneComponent*>(
                Objects.at(Relation.AttachParentId.Value));
            if (!Component->AttachToComponent(
                    Parent,
                    EAttachmentTransformRule::KeepRelative))
            {
                return Fail(EWorldSerializationError::RelationRestoreFailed);
            }
        }

        try
        {
            for (const FSceneObjectRecord& Record : Data.Objects)
            {
                Objects.at(Record.Id.Value)->PostLoad();
            }
        }
        catch (...)
        {
            return Fail(EWorldSerializationError::PostLoadFailed);
        }

        ReportError(OutError, EWorldSerializationError::None);
        return World;
    }
};

std::string_view ToString(EWorldSerializationError Error)
{
    switch (Error)
    {
    case EWorldSerializationError::None:
        return "None";
    case EWorldSerializationError::InvalidArgument:
        return "InvalidArgument";
    case EWorldSerializationError::InvalidArchive:
        return "InvalidArchive";
    case EWorldSerializationError::UnsupportedVersion:
        return "UnsupportedVersion";
    case EWorldSerializationError::ObjectLimitExceeded:
        return "ObjectLimitExceeded";
    case EWorldSerializationError::RelationLimitExceeded:
        return "RelationLimitExceeded";
    case EWorldSerializationError::PropertyLimitExceeded:
        return "PropertyLimitExceeded";
    case EWorldSerializationError::PropertyAccessFailed:
        return "PropertyAccessFailed";
    case EWorldSerializationError::PropertyTypeMismatch:
        return "PropertyTypeMismatch";
    case EWorldSerializationError::ClassNotFound:
        return "ClassNotFound";
    case EWorldSerializationError::InvalidObjectGraph:
        return "InvalidObjectGraph";
    case EWorldSerializationError::ObjectCreationFailed:
        return "ObjectCreationFailed";
    case EWorldSerializationError::RelationRestoreFailed:
        return "RelationRestoreFailed";
    case EWorldSerializationError::PostLoadFailed:
        return "PostLoadFailed";
    case EWorldSerializationError::FileOpenFailed:
        return "FileOpenFailed";
    case EWorldSerializationError::FileReadFailed:
        return "FileReadFailed";
    case EWorldSerializationError::FileWriteFailed:
        return "FileWriteFailed";
    case EWorldSerializationError::FileTooLarge:
        return "FileTooLarge";
    case EWorldSerializationError::TrailingData:
        return "TrailingData";
    }
    return "Unknown";
}

bool CaptureWorld(
    const PWorld& World,
    FWorldAssetData& OutData,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (World.GetState() != EWorldState::Initialized)
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    FWorldAssetData Data;
    std::unordered_map<const PObject*, FSceneObjectId> ObjectIds;
    if (!AddObjectRecord(World, {}, Data, ObjectIds, OutError))
    {
        return false;
    }
    Data.WorldId = ObjectIds.at(&World);

    for (PLevel* Level : World.GetLevels())
    {
        if (Level == nullptr)
        {
            ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
            return false;
        }
        if (!AddObjectRecord(*Level, Data.WorldId, Data, ObjectIds, OutError))
        {
            return false;
        }

        const FSceneObjectId LevelId = ObjectIds.at(Level);
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr)
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }
            if (!AddObjectRecord(*Actor, LevelId, Data, ObjectIds, OutError))
            {
                return false;
            }

            const FSceneObjectId ActorId = ObjectIds.at(Actor);
            for (PActorComponent* Component : Actor->GetComponents())
            {
                if (Component == nullptr)
                {
                    ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                    return false;
                }
                if (!AddObjectRecord(*Component, ActorId, Data, ObjectIds, OutError))
                {
                    return false;
                }
            }
        }
    }

    const auto PersistentLevel = ObjectIds.find(World.GetPersistentLevel());
    const auto CurrentLevel = ObjectIds.find(World.GetCurrentLevel());
    if (PersistentLevel == ObjectIds.end() || CurrentLevel == ObjectIds.end())
    {
        ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
        return false;
    }
    Data.PersistentLevelId = PersistentLevel->second;
    Data.CurrentLevelId = CurrentLevel->second;

    for (PLevel* Level : World.GetLevels())
    {
        for (PActor* Actor : Level->GetActors())
        {
            if (PSceneComponent* RootComponent = Actor->GetRootComponent())
            {
                const auto RootId = ObjectIds.find(RootComponent);
                if (RootId == ObjectIds.end())
                {
                    ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                    return false;
                }
                Data.Relations.push_back(
                    FSceneRelationRecord { ObjectIds.at(Actor), RootId->second, {} });
            }

            for (PActorComponent* Component : Actor->GetComponents())
            {
                if (!Component->IsA(PSceneComponent::StaticClass()))
                {
                    continue;
                }

                PSceneComponent* SceneComponent =
                    static_cast<PSceneComponent*>(Component);
                if (PSceneComponent* AttachParent = SceneComponent->GetAttachParent())
                {
                    const auto ParentId = ObjectIds.find(AttachParent);
                    if (ParentId == ObjectIds.end())
                    {
                        ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                        return false;
                    }
                    Data.Relations.push_back(
                        FSceneRelationRecord {
                            ObjectIds.at(SceneComponent),
                            {},
                            ParentId->second });
                }
            }
        }
    }

    if (Data.Relations.size() > MaxSceneRelationCount
        || !ValidateWorldAssetData(Data, OutError))
    {
        if (Data.Relations.size() > MaxSceneRelationCount)
        {
            ReportError(OutError, EWorldSerializationError::RelationLimitExceeded);
        }
        return false;
    }

    OutData = std::move(Data);
    return true;
}

bool ValidateWorldAssetData(
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (!Data.WorldId.IsValid()
        || !Data.PersistentLevelId.IsValid()
        || !Data.CurrentLevelId.IsValid()
        || Data.Objects.empty())
    {
        ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
        return false;
    }
    if (Data.Objects.size() > MaxSceneObjectCount)
    {
        ReportError(OutError, EWorldSerializationError::ObjectLimitExceeded);
        return false;
    }
    if (Data.Relations.size() > MaxSceneRelationCount)
    {
        ReportError(OutError, EWorldSerializationError::RelationLimitExceeded);
        return false;
    }

    std::unordered_map<uint64, const FSceneObjectRecord*> Records;
    std::unordered_set<std::string> ObjectNames;
    for (const FSceneObjectRecord& Record : Data.Objects)
    {
        if (!Record.Id.IsValid()
            || Record.ClassName.empty()
            || Record.ObjectName.empty()
            || Record.ClassName.size() > MaxSceneNameLength
            || Record.ObjectName.size() > MaxSceneNameLength
            || !Records.emplace(Record.Id.Value, &Record).second
            || !IsObjectNameUnique(Record, ObjectNames))
        {
            ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
            return false;
        }
        if (Record.Properties.size() > MaxObjectPropertyCount)
        {
            ReportError(OutError, EWorldSerializationError::PropertyLimitExceeded);
            return false;
        }

        std::unordered_set<std::string> PropertyNames;
        for (const FSerializedPropertyRecord& Property : Record.Properties)
        {
            if (Property.Name.empty()
                || Property.Name.size() > MaxSceneNameLength
                || !IsValidSerializedPropertyType(Property.Type)
                || !PropertyNames.insert(Property.Name).second)
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }
        }
    }

    const FSceneObjectRecord* WorldRecord = FindRecord(Records, Data.WorldId);
    const FSceneObjectRecord* PersistentLevel =
        FindRecord(Records, Data.PersistentLevelId);
    const FSceneObjectRecord* CurrentLevel =
        FindRecord(Records, Data.CurrentLevelId);
    if (WorldRecord == nullptr
        || PersistentLevel == nullptr
        || CurrentLevel == nullptr)
    {
        ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
        return false;
    }

    for (const FSceneObjectRecord& Record : Data.Objects)
    {
        if (FindRecordClass(Record) == nullptr)
        {
            ReportError(OutError, EWorldSerializationError::ClassNotFound);
            return false;
        }
        if (Record.Id == Data.WorldId)
        {
            if (Record.OuterId.IsValid()
                || !IsChildOf(Record, PWorld::StaticClass()))
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }
        }
        else
        {
            const FSceneObjectRecord* Outer = FindRecord(Records, Record.OuterId);
            if (Outer == nullptr)
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }

            const bool bValidLevel =
                IsChildOf(Record, PLevel::StaticClass())
                && Outer->Id == Data.WorldId;
            const bool bValidActor =
                IsChildOf(Record, PActor::StaticClass())
                && IsChildOf(*Outer, PLevel::StaticClass());
            const bool bValidComponent =
                IsChildOf(Record, PActorComponent::StaticClass())
                && IsChildOf(*Outer, PActor::StaticClass());
            if (!bValidLevel && !bValidActor && !bValidComponent)
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }
        }

        if (HasOuterCycle(Record, Records))
        {
            ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
            return false;
        }
    }

    if (!IsChildOf(*PersistentLevel, PLevel::StaticClass())
        || PersistentLevel->OuterId != Data.WorldId
        || !IsChildOf(*CurrentLevel, PLevel::StaticClass())
        || CurrentLevel->OuterId != Data.WorldId)
    {
        ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
        return false;
    }

    std::unordered_set<uint64> RelatedObjects;
    std::unordered_set<uint64> RootComponents;
    std::unordered_map<uint64, uint64> AttachParents;
    for (const FSceneRelationRecord& Relation : Data.Relations)
    {
        const FSceneObjectRecord* Object = FindRecord(Records, Relation.ObjectId);
        if (Object == nullptr
            || !RelatedObjects.insert(Relation.ObjectId.Value).second
            || (Relation.RootComponentId.IsValid()
                == Relation.AttachParentId.IsValid()))
        {
            ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
            return false;
        }

        if (Relation.RootComponentId.IsValid())
        {
            const FSceneObjectRecord* Root =
                FindRecord(Records, Relation.RootComponentId);
            if (Root == nullptr
                || !IsChildOf(*Object, PActor::StaticClass())
                || !IsChildOf(*Root, PSceneComponent::StaticClass())
                || Root->OuterId != Object->Id
                || !RootComponents.insert(Root->Id.Value).second)
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }
        }
        else
        {
            const FSceneObjectRecord* Parent =
                FindRecord(Records, Relation.AttachParentId);
            if (Parent == nullptr
                || !IsChildOf(*Object, PSceneComponent::StaticClass())
                || !IsChildOf(*Parent, PSceneComponent::StaticClass())
                || Object->OuterId != Parent->OuterId
                || !AttachParents.emplace(
                        Object->Id.Value,
                        Parent->Id.Value).second)
            {
                ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
                return false;
            }
        }
    }

    for (const auto& [ObjectId, ParentId] : AttachParents)
    {
        (void)ParentId;
        if (RootComponents.contains(ObjectId)
            || HasAttachmentCycle(ObjectId, AttachParents))
        {
            ReportError(OutError, EWorldSerializationError::InvalidObjectGraph);
            return false;
        }
    }

    return true;
}

bool SerializeWorldAsset(
    FArchive& Archive,
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (!Archive.IsSaving())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }
    if (!ValidateWorldAssetData(Data, OutError))
    {
        return false;
    }

    FWorldAssetData SerializedData = Data;
    uint32 Magic = WorldMagic;
    uint32 Version = WorldFormatVersion;
    uint32 ObjectCount = static_cast<uint32>(SerializedData.Objects.size());
    uint32 RelationCount = static_cast<uint32>(SerializedData.Relations.size());
    Archive.SerializeUInt32(Magic);
    Archive.SerializeUInt32(Version);
    Archive.SerializeUInt32(ObjectCount);
    Archive.SerializeUInt32(RelationCount);
    Archive.SerializeUInt64(SerializedData.WorldId.Value);
    Archive.SerializeUInt64(SerializedData.PersistentLevelId.Value);
    Archive.SerializeUInt64(SerializedData.CurrentLevelId.Value);

    for (FSceneObjectRecord& Record : SerializedData.Objects)
    {
        if (!SerializeObjectRecord(Archive, Record))
        {
            ReportError(OutError, EWorldSerializationError::InvalidArchive);
            return false;
        }
    }
    for (FSceneRelationRecord& Relation : SerializedData.Relations)
    {
        SerializeRelationRecord(Archive, Relation);
    }
    if (Archive.HasError())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArchive);
        return false;
    }
    return true;
}

bool DeserializeWorldAsset(
    FArchive& Archive,
    FWorldAssetData& OutData,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (!Archive.IsLoading())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    uint32 Magic = 0;
    uint32 Version = 0;
    uint32 ObjectCount = 0;
    uint32 RelationCount = 0;
    Archive.SerializeUInt32(Magic);
    Archive.SerializeUInt32(Version);
    Archive.SerializeUInt32(ObjectCount);
    Archive.SerializeUInt32(RelationCount);
    if (Archive.HasError() || Magic != WorldMagic)
    {
        ReportError(OutError, EWorldSerializationError::InvalidArchive);
        return false;
    }
    if (Version != WorldFormatVersion)
    {
        ReportError(OutError, EWorldSerializationError::UnsupportedVersion);
        return false;
    }
    if (ObjectCount == 0 || ObjectCount > MaxSceneObjectCount)
    {
        ReportError(OutError, EWorldSerializationError::ObjectLimitExceeded);
        return false;
    }
    if (RelationCount > MaxSceneRelationCount)
    {
        ReportError(OutError, EWorldSerializationError::RelationLimitExceeded);
        return false;
    }

    FWorldAssetData Data;
    Archive.SerializeUInt64(Data.WorldId.Value);
    Archive.SerializeUInt64(Data.PersistentLevelId.Value);
    Archive.SerializeUInt64(Data.CurrentLevelId.Value);
    Data.Objects.resize(ObjectCount);
    for (FSceneObjectRecord& Record : Data.Objects)
    {
        if (!SerializeObjectRecord(Archive, Record))
        {
            ReportError(
                OutError,
                Archive.HasError()
                    ? EWorldSerializationError::InvalidArchive
                    : EWorldSerializationError::PropertyLimitExceeded);
            return false;
        }
    }

    Data.Relations.resize(RelationCount);
    for (FSceneRelationRecord& Relation : Data.Relations)
    {
        SerializeRelationRecord(Archive, Relation);
    }
    if (Archive.HasError())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArchive);
        return false;
    }
    if (!ValidateWorldAssetData(Data, OutError))
    {
        return false;
    }

    OutData = std::move(Data);
    return true;
}

PWorld* CreateWorldFromAssetData(
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    return FWorldAssetLoader::Create(Data, OutError);
}

bool SaveWorldToFile(
    const std::filesystem::path& FilePath,
    const PWorld& World,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (FilePath.empty())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    FWorldAssetData Data;
    if (!CaptureWorld(World, Data, OutError))
    {
        return false;
    }

    FMemoryWriter Writer;
    if (!SerializeWorldAsset(Writer, Data, OutError))
    {
        return false;
    }

    std::filesystem::path TemporaryPath = FilePath;
    TemporaryPath += ".tmp";
    std::ofstream File(TemporaryPath, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        ReportError(OutError, EWorldSerializationError::FileOpenFailed);
        return false;
    }

    const std::vector<uint8>& Bytes = Writer.GetData();
    File.write(
        reinterpret_cast<const char*>(Bytes.data()),
        static_cast<std::streamsize>(Bytes.size()));
    File.flush();
    const bool bWriteSucceeded = File.good();
    File.close();
    if (!bWriteSucceeded)
    {
        std::error_code ErrorCode;
        std::filesystem::remove(TemporaryPath, ErrorCode);
        ReportError(OutError, EWorldSerializationError::FileWriteFailed);
        return false;
    }

    if (!Detail::ReplaceSerializedFile(TemporaryPath, FilePath))
    {
        std::error_code ErrorCode;
        std::filesystem::remove(TemporaryPath, ErrorCode);
        ReportError(OutError, EWorldSerializationError::FileWriteFailed);
        return false;
    }

    return true;
}

PWorld* LoadWorldFromFile(
    const std::filesystem::path& FilePath,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (FilePath.empty())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return nullptr;
    }

    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File)
    {
        ReportError(OutError, EWorldSerializationError::FileOpenFailed);
        return nullptr;
    }

    const std::streampos EndPosition = File.tellg();
    if (EndPosition < 0)
    {
        ReportError(OutError, EWorldSerializationError::FileReadFailed);
        return nullptr;
    }
    if (static_cast<std::size_t>(EndPosition) > MaxWorldFileSize)
    {
        ReportError(OutError, EWorldSerializationError::FileTooLarge);
        return nullptr;
    }

    std::vector<uint8> Bytes(static_cast<std::size_t>(EndPosition));
    File.seekg(0, std::ios::beg);
    if (!Bytes.empty())
    {
        File.read(
            reinterpret_cast<char*>(Bytes.data()),
            static_cast<std::streamsize>(Bytes.size()));
    }
    if (!File)
    {
        ReportError(OutError, EWorldSerializationError::FileReadFailed);
        return nullptr;
    }

    FMemoryReader Reader(Bytes);
    FWorldAssetData Data;
    if (!DeserializeWorldAsset(Reader, Data, OutError))
    {
        return nullptr;
    }
    if (Reader.GetRemainingSize() != 0)
    {
        ReportError(OutError, EWorldSerializationError::TrailingData);
        return nullptr;
    }

    return CreateWorldFromAssetData(Data, OutError);
}
}
