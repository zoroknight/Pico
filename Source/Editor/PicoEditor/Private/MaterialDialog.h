#pragma once

#include "AssetReferenceWidget.h"

#include "Pico/Asset/Material.h"

#include <optional>

namespace Pico
{
struct FMaterialSaveRequest
{
    FAssetPath AssetPath;
    FMaterialData Material;
    bool bCreate = false;
};

class FMaterialDialog
{
public:
    void OpenCreate(
        FAssetPath AssetPath,
        FAssetPath SelectedTexture,
        const FAssetRegistry& AssetRegistry);
    void OpenEdit(
        FAssetPath AssetPath,
        const FMaterialData& Material,
        FAssetPath SelectedTexture,
        const FAssetRegistry& AssetRegistry);
    std::optional<FMaterialSaveRequest> Draw();

private:
    FAssetPath AssetPath;
    FAssetPath SelectedTexture;
    const FAssetRegistry* AssetRegistry = nullptr;
    FMaterialData Material;
    FAssetReferenceWidget AssetReferenceWidget;
    bool bCreate = false;
    bool bOpenPopup = false;
};
}
