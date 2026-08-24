#include "Pico/Editor/EditorPropertyService.h"

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Pico
{
namespace
{
FEditorPropertyResult Failure(std::string Message)
{
    return {false, std::move(Message)};
}

bool MatchesAssetType(EAssetType AssetType, EAssetReferenceType ReferenceType)
{
    switch (ReferenceType)
    {
    case EAssetReferenceType::StaticMesh:
        return AssetType == EAssetType::StaticMesh;
    case EAssetReferenceType::SkeletalMesh:
        return AssetType == EAssetType::SkeletalMesh;
    case EAssetReferenceType::AnimationClip:
        return AssetType == EAssetType::AnimationClip;
    case EAssetReferenceType::AnimationSet:
        return AssetType == EAssetType::AnimationSet;
    case EAssetReferenceType::AnimationMontage:
        return AssetType == EAssetType::AnimationMontage;
    case EAssetReferenceType::CharacterProfile:
        return AssetType == EAssetType::CharacterProfile;
    case EAssetReferenceType::ThirdPersonControlProfile:
        return AssetType == EAssetType::ThirdPersonControlProfile;
    case EAssetReferenceType::Texture:
        return AssetType == EAssetType::Texture;
    case EAssetReferenceType::Material:
        return AssetType == EAssetType::Material;
    case EAssetReferenceType::None:
        return true;
    }
    return false;
}

bool ApplyValue(
    PObject* Object,
    const PProperty* Property,
    const FEditorPropertyValue& Value)
{
    switch (Property->GetType())
    {
    case EPropertyType::Int32:
        if (const int32* Typed = std::get_if<int32>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::Float:
        if (const float* Typed = std::get_if<float>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::Bool:
        if (const bool* Typed = std::get_if<bool>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::Vector3:
        if (const FVector3* Typed = std::get_if<FVector3>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::Rotator:
        if (const FRotator* Typed = std::get_if<FRotator>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::Transform:
        if (const FTransform* Typed = std::get_if<FTransform>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::AssetPath:
        if (const FAssetPath* Typed = std::get_if<FAssetPath>(&Value))
            return Property->SetValue(Object, *Typed);
        break;
    case EPropertyType::Object:
    case EPropertyType::DynamicMulticastDelegate:
        break;
    }
    return false;
}

bool IsFinite(const FVector3& Value)
{
    return std::isfinite(Value.X) && std::isfinite(Value.Y)
        && std::isfinite(Value.Z);
}

bool IsFinite(const FRotator& Value)
{
    return std::isfinite(Value.Pitch) && std::isfinite(Value.Yaw)
        && std::isfinite(Value.Roll);
}

bool IsFinite(const FTransform& Value)
{
    return IsFinite(Value.Translation) && IsFinite(Value.Scale)
        && std::isfinite(Value.Rotation.X) && std::isfinite(Value.Rotation.Y)
        && std::isfinite(Value.Rotation.Z) && std::isfinite(Value.Rotation.W);
}

bool IsWithinMetadataRange(
    const FPropertyMetadata& Metadata,
    const FEditorPropertyValue& Value)
{
    const auto InRange = [&Metadata](double Number)
    {
        return std::isfinite(Number)
            && (!Metadata.Minimum || Number >= *Metadata.Minimum)
            && (!Metadata.Maximum || Number <= *Metadata.Maximum);
    };
    if (const int32* Typed = std::get_if<int32>(&Value)) return InRange(*Typed);
    if (const float* Typed = std::get_if<float>(&Value)) return InRange(*Typed);
    if (const FVector3* Typed = std::get_if<FVector3>(&Value))
        return InRange(Typed->X) && InRange(Typed->Y) && InRange(Typed->Z);
    if (const FRotator* Typed = std::get_if<FRotator>(&Value))
        return IsFinite(*Typed);
    if (const FTransform* Typed = std::get_if<FTransform>(&Value))
        return IsFinite(*Typed);
    return true;
}
}

FEditorPropertyResult ApplyEditorPropertyValue(
    FEngineLoop* EngineLoop,
    PObject* Object,
    const PProperty* Property,
    const FEditorPropertyValue& Value)
{
    if (Object == nullptr || Property == nullptr)
        return Failure("Object or property is no longer available");
    if (!Property->HasAnyFlags(EPropertyFlags::Editable)
        || Property->HasAnyFlags(EPropertyFlags::ReadOnly))
    {
        return Failure("Property is not editable");
    }
    const FPropertyMetadata& Metadata = Property->GetMetadata();
    if (!IsWithinMetadataRange(Metadata, Value))
        return Failure("Property value is outside its reflected range");
    if (const int32* EnumValue = std::get_if<int32>(&Value);
        EnumValue != nullptr && !Metadata.EnumOptions.empty())
    {
        const bool bFound = std::any_of(
            Metadata.EnumOptions.begin(), Metadata.EnumOptions.end(),
            [EnumValue](const FPropertyMetadata::FEnumOption& Option)
            {
                return Option.Value == *EnumValue;
            });
        if (!bFound) return Failure("Property enum value is not allowed");
    }
    if (Property->GetType() == EPropertyType::AssetPath)
    {
        const FAssetPath* AssetPath = std::get_if<FAssetPath>(&Value);
        const FAssetRecord* Record = EngineLoop != nullptr && AssetPath != nullptr
            && AssetPath->IsValid()
            ? EngineLoop->GetAssetRegistry().Find(*AssetPath) : nullptr;
        if (AssetPath == nullptr
            || (AssetPath->IsValid()
                && (Record == nullptr
                    || !MatchesAssetType(
                        Record->Type, Property->GetAssetReferenceType()))))
        {
            return Failure("Asset type is not valid for this property");
        }
    }
    if (!ApplyValue(Object, Property, Value))
        return Failure("Could not apply property value");
    return {true, "Changed " + Object->GetPathName() + "."
        + Property->GetName().ToString()};
}

FEditorPropertyService::FEditorPropertyService(
    FEngineLoop* InEngineLoop,
    FEditorSelection* InSelection,
    FEditorTransactionManager* InTransactions,
    FEditorTransactionManager::FRestoreSnapshot InRestoreSnapshot)
    : EngineLoop(InEngineLoop)
    , Selection(InSelection)
    , Transactions(InTransactions)
    , RestoreSnapshot(std::move(InRestoreSnapshot))
{
}

FEditorPropertyResult FEditorPropertyService::SetProperty(
    FObjectHandle ObjectHandle,
    FName PropertyName,
    const FEditorPropertyValue& Value)
{
    PObject* Object = ResolveObject(ObjectHandle);
    const PProperty* Property = Object != nullptr && Object->GetClass() != nullptr
        ? Object->GetClass()->FindProperty(PropertyName) : nullptr;
    if (Object == nullptr || Property == nullptr)
    {
        return Failure("Object or property is no longer available");
    }
    PWorld* World = EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
    if (World == nullptr || Selection == nullptr || Transactions == nullptr)
    {
        return Failure("Editor property service is not initialized");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    const std::string Description =
        "Edit " + Object->GetPathName() + "." + PropertyName.ToString();
    if (!Transactions->Begin(
            Description,
            *World,
            Selection->GetObjectPaths(),
            Selection->GetObjectPath(),
            &Error))
    {
        return Failure("Could not begin property transaction");
    }

    FEditorPropertyResult ApplyResult;
    try
    {
        ApplyResult = ApplyEditorPropertyValue(
            EngineLoop, Object, Property, Value);
    }
    catch (...)
    {
        ApplyResult = Failure("Could not apply property value");
    }
    if (!ApplyResult.bSucceeded)
    {
        Transactions->Rollback(RestoreSnapshot, &Error);
        return ApplyResult;
    }
    if (!Transactions->Commit(
            *World,
            Selection->GetObjectPaths(),
            Selection->GetObjectPath(),
            &Error))
    {
        Transactions->Rollback(RestoreSnapshot, &Error);
        return Failure("Could not commit property transaction");
    }
    return {true, "Changed " + Object->GetPathName() + "." + PropertyName.ToString()};
}

}
