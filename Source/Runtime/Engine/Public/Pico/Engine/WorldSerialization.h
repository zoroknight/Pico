#pragma once

#include "Pico/Core/Types.h"
#include "Pico/Object/Archive.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/SerializedProperty.h"

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

struct FSceneObjectRecord
{
    FSceneObjectId Id;
    FSceneObjectId OuterId;
    std::string ClassName;
    std::string ObjectName;
    EObjectFlags Flags = EObjectFlags::None;
    std::vector<FSerializedPropertyRecord> Properties;
};

struct FSceneRelationRecord
{
    FSceneObjectId ObjectId;
    FSceneObjectId RootComponentId;
    FSceneObjectId AttachParentId;
};

struct FWorldAssetData
{
    FSceneObjectId WorldId;
    FSceneObjectId PersistentLevelId;
    FSceneObjectId CurrentLevelId;
    std::vector<FSceneObjectRecord> Objects;
    std::vector<FSceneRelationRecord> Relations;
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
    PostLoadFailed
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
    EWorldSerializationError* OutError = nullptr);
}
