#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorAssetSelection.h"

#include <array>
#include <functional>
#include <string>

namespace Pico
{
class FContentBrowserPanel
{
public:
    using FAction = std::function<void()>;
    using FAssetAction = std::function<void(const FAssetPath&)>;
    using FAssetsAction = std::function<void(const std::vector<FAssetPath>&)>;
    using FCanReimport = std::function<bool(const FAssetPath&)>;

    void Draw(
        const FAssetRegistry& Registry,
        FEditorAssetSelection& Selection,
        FAction Import,
        FAction ImportTexture,
        FAction CreateMaterial,
        FAction Refresh,
        FAssetAction Reimport,
        FAssetAction ReimportWithOptions,
        FAssetsAction DeleteAssets,
        FAssetAction RenameAsset,
        FAssetAction EditMaterial,
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
        const FAssetAction& Create,
        const FAssetAction& Assign,
        const FCanReimport& CanReimport);

    std::array<char, 192> SearchBuffer {};
    std::string CurrentFolder;
    int TypeFilter = 0;
    bool bKeyboardFocused = false;
};
}
