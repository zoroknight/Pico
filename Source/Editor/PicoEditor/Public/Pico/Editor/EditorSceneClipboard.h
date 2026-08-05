#pragma once

#include "Pico/Engine/WorldSerialization.h"

#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class PWorld;

enum class EEditorClipboardContentType
{
    None,
    Actor,
    SceneComponent
};

enum class EEditorClipboardError
{
    None,
    InvalidArgument,
    ObjectNotFound,
    UnsupportedObject,
    EmptyClipboard,
    InvalidDestination,
    IdOverflow,
    InvalidSceneData
};

std::string_view ToString(EEditorClipboardError Error);

class FEditorSceneClipboard
{
public:
    bool Copy(
        const PWorld& World,
        std::string_view ObjectPath,
        EEditorClipboardError* OutError = nullptr);
    bool Copy(
        const PWorld& World,
        const std::vector<std::string>& ObjectPaths,
        EEditorClipboardError* OutError = nullptr);
    bool BuildPaste(
        const PWorld& World,
        std::string_view DestinationPath,
        FWorldAssetData& OutWorldData,
        std::string& OutPastedObjectPath,
        EEditorClipboardError* OutError = nullptr) const;
    bool BuildPaste(
        const PWorld& World,
        std::string_view DestinationPath,
        FWorldAssetData& OutWorldData,
        std::vector<std::string>& OutPastedObjectPaths,
        EEditorClipboardError* OutError = nullptr) const;

    void Clear();
    bool HasContent() const;
    EEditorClipboardContentType GetContentType() const;
    std::string_view GetSourceObjectPath() const;

private:
    EEditorClipboardContentType ContentType =
        EEditorClipboardContentType::None;
    std::vector<FSceneObjectId> RootObjectIds;
    std::vector<std::string> SourceObjectPaths;
    std::vector<FSceneObjectRecord> Objects;
    std::vector<FSceneRelationRecord> Relations;
};
}
