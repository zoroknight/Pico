#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/WorldSerialization.h"

#include <filesystem>
#include <string>

namespace Pico
{
class FEngineLoop;

struct FEditorDocumentResult
{
    bool bSucceeded = false;
    std::string Message;
};

class FEditorWorldDocument
{
public:
    explicit FEditorWorldDocument(FEngineLoop* EngineLoop);

    FEditorDocumentResult NewWorld();
    FEditorDocumentResult Open(const std::filesystem::path& FilePath);
    FEditorDocumentResult Open(const FAssetPath& AssetPath);
    FEditorDocumentResult Save();
    FEditorDocumentResult SaveAs(const std::filesystem::path& FilePath);

    void MarkDirty();
    bool HasAssetPath() const;
    bool IsDirty() const;
    const FAssetPath& GetAssetPath() const;
    const std::filesystem::path& GetFilePath() const;
    std::string GetDisplayName() const;

    static bool TryResolveFilePath(
        const FAssetPath& AssetPath,
        std::filesystem::path& OutFilePath);
    static bool TryMakeAssetPath(
        const std::filesystem::path& FilePath,
        FAssetPath& OutAssetPath);

private:
    void SetCurrentFile(FAssetPath AssetPath, std::filesystem::path FilePath);
    void RefreshAssetRegistry();

    FEngineLoop* EngineLoop = nullptr;
    FWorldAssetData EmptyWorldData;
    FAssetPath CurrentAssetPath;
    std::filesystem::path CurrentFilePath;
    bool bHasEmptyWorldData = false;
    bool bDirty = false;
};
}
