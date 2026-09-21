#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorAssetSelection.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Pico
{
class FContentBrowserPanel
{
public:
    using FAction = std::function<void()>;
    using FAssetAction = std::function<void(const FAssetPath&)>;
    using FAssetsAction = std::function<void(const std::vector<FAssetPath>&)>;
    using FCanReimport = std::function<bool(const FAssetPath&)>;

    FContentBrowserPanel() = default;
    ~FContentBrowserPanel();

    void Draw(
        const FAssetRegistry& Registry,
        FEditorAssetSelection& Selection,
        FAction Import,
        FAction ImportSkeletal,
        FAction ImportTexture,
        FAction CreateMaterial,
        FAction CreateActorBlueprint,
        FAction CreatePicoGraph,
        FAction Refresh,
        FAssetAction Reimport,
        FAssetAction ReimportWithOptions,
        FAssetsAction DeleteAssets,
        FAssetAction RenameAsset,
        FAssetAction EditMaterial,
        FAssetAction OpenSkeletal,
        FAssetAction OpenActorBlueprint,
        FAssetAction OpenPicoGraph,
        FAssetAction OpenWorld,
        FAssetAction Create,
        FAssetAction Assign,
        FCanReimport CanReimport);
    bool IsKeyboardFocused() const;
    bool FocusAsset(
        const FAssetRegistry& Registry,
        FEditorAssetSelection& Selection,
        const FAssetPath& AssetPath);
    void SelectAllVisible(
        const FAssetRegistry& Registry,
        FEditorAssetSelection& Selection) const;

private:
    enum class EViewMode
    {
        List,
        Tiles
    };

    struct FThumbnailCacheEntry
    {
        std::filesystem::file_time_type LastWriteTime {};
        std::uint32_t Texture = 0;
        std::vector<std::array<float, 2>> MeshLines;
        std::array<float, 3> Color {0.25f, 0.28f, 0.30f};
        bool bLoaded = false;
    };

    bool PassesFilter(const FAssetRecord& Record) const;
    void DrawFolderTree(const FAssetRegistry& Registry);
    void DrawAssetTable(
        const FAssetRegistry& Registry,
        FEditorAssetSelection& Selection,
        const FAssetAction& Reimport,
        const FAssetAction& ReimportWithOptions,
        const FAssetsAction& DeleteAssets,
        const FAssetAction& RenameAsset,
        const FAssetAction& EditMaterial,
        const FAssetAction& OpenSkeletal,
        const FAssetAction& OpenActorBlueprint,
        const FAssetAction& OpenPicoGraph,
        const FAssetAction& OpenWorld,
        const FAssetAction& Create,
        const FAssetAction& Assign,
        const FCanReimport& CanReimport);
    void DrawAssetTiles(
        const FAssetRegistry& Registry,
        FEditorAssetSelection& Selection,
        const FAssetAction& Reimport,
        const FAssetAction& ReimportWithOptions,
        const FAssetsAction& DeleteAssets,
        const FAssetAction& RenameAsset,
        const FAssetAction& EditMaterial,
        const FAssetAction& OpenSkeletal,
        const FAssetAction& OpenActorBlueprint,
        const FAssetAction& OpenPicoGraph,
        const FAssetAction& OpenWorld,
        const FAssetAction& Create,
        const FAssetAction& Assign,
        const FCanReimport& CanReimport);
    FThumbnailCacheEntry& GetThumbnail(const FAssetRecord& Record);
    void ReleaseThumbnails();

    std::array<char, 192> SearchBuffer {};
    std::string CurrentFolder;
    std::unordered_map<std::string, FThumbnailCacheEntry> Thumbnails;
    int TypeFilter = 0;
    EViewMode ViewMode = EViewMode::List;
    bool bKeyboardFocused = false;
};
}
