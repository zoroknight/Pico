#include "Pico/Editor/EditorPropertyService.h"

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"

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
    if (!Property->HasAnyFlags(EPropertyFlags::Editable)
        || Property->HasAnyFlags(EPropertyFlags::ReadOnly))
    {
        return Failure("Property is not editable");
    }
    if (Property->GetType() == EPropertyType::AssetPath)
    {
        const FAssetPath* AssetPath = std::get_if<FAssetPath>(&Value);
        if (AssetPath == nullptr || !ValidateAssetReference(*Property, *AssetPath))
        {
            return Failure("Asset type is not valid for this property");
        }
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

    bool bApplied = false;
    try
    {
        bApplied = ApplyValue(Object, Property, Value);
    }
    catch (...)
    {
        bApplied = false;
    }
    if (!bApplied)
    {
        Transactions->Rollback(RestoreSnapshot, &Error);
        return Failure("Could not apply property value");
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

bool FEditorPropertyService::ValidateAssetReference(
    const PProperty& Property,
    const FAssetPath& AssetPath) const
{
    if (!AssetPath.IsValid())
    {
        return true;
    }
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    return Record != nullptr
        && MatchesAssetType(Record->Type, Property.GetAssetReferenceType());
}
}
