#pragma once

#include "Pico/Core/Types.h"
#include "Pico/Object/Archive.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/SerializedProperty.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class PWorld;

struct FSceneObjectId
{
    bool IsValid() const
    {
        return Value != 0;
    }

    friend bool operator==(const FSceneObjectId&, const FSceneObjectId&) = default;

    uint64 Value = 0;
};

struct FSerializedObjectReference
{
    FSceneObjectId SceneId;
    std::string ObjectPath;
};

struct FSerializedDynamicDelegateBinding
{
    FSerializedObjectReference Target;
    std::string FunctionName;
};

struct FSerializedDynamicDelegateRecord
{
    std::string PropertyName;
    std::vector<FSerializedDynamicDelegateBinding> Bindings;
};

struct FSceneObjectRecord
{
    FSceneObjectId Id;
    FSceneObjectId OuterId;
    std::string ClassName;
    std::string ObjectName;
    EObjectFlags Flags = EObjectFlags::None;
    std::vector<FSerializedPropertyRecord> Properties;
    std::vector<FSerializedDynamicDelegateRecord> DynamicDelegates;
};

struct FSceneRelationRecord
{
    FSceneObjectId ObjectId;
    FSceneObjectId RootComponentId;
    FSceneObjectId AttachParentId;
    std::string AttachSocketName;
};

struct FWorldAssetData
{
    FSceneObjectId WorldId;
    FSceneObjectId PersistentLevelId;
    FSceneObjectId CurrentLevelId;
    std::vector<FSceneObjectRecord> Objects;
    std::vector<FSceneRelationRecord> Relations;
};

struct FWorldLoadOptions
{
    EPropertyChangeType PropertyChangeType = EPropertyChangeType::Load;
};

enum class EWorldSerializationError
{
    None,
    InvalidArgument,
    InvalidArchive,
    UnsupportedVersion,
    ObjectLimitExceeded,
    RelationLimitExceeded,
    PropertyLimitExceeded,
    PropertyAccessFailed,
    PropertyTypeMismatch,
    ClassNotFound,
    InvalidObjectGraph,
    ObjectCreationFailed,
    RelationRestoreFailed,
    PostLoadFailed,
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileTooLarge,
    TrailingData,
    WorldReplacementFailed
};

std::string_view ToString(EWorldSerializationError Error);

bool CaptureWorld(
    const PWorld& World,
    FWorldAssetData& OutData,
    EWorldSerializationError* OutError = nullptr);
bool ValidateWorldAssetData(
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError = nullptr);
bool SerializeWorldAsset(
    FArchive& Archive,
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError = nullptr);
bool DeserializeWorldAsset(
    FArchive& Archive,
    FWorldAssetData& OutData,
    EWorldSerializationError* OutError = nullptr);
PWorld* CreateWorldFromAssetData(
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError = nullptr,
    FWorldLoadOptions Options = {});
bool SaveWorldToFile(
    const std::filesystem::path& FilePath,
    const PWorld& World,
    EWorldSerializationError* OutError = nullptr);
bool SaveWorldAssetDataToFile(
    const std::filesystem::path& FilePath,
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError = nullptr);
bool LoadWorldAssetDataFromFile(
    const std::filesystem::path& FilePath,
    FWorldAssetData& OutData,
    EWorldSerializationError* OutError = nullptr);
PWorld* LoadWorldFromFile(
    const std::filesystem::path& FilePath,
    EWorldSerializationError* OutError = nullptr);
}
