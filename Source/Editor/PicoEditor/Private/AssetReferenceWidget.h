#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Object/Property.h"

#include <array>
#include <string>
#include <unordered_map>

namespace Pico
{
struct FAssetReferenceEditResult
{
    FAssetPath Value;
    bool bChanged = false;
    bool bBrowseRequested = false;
    bool bRejectedDrop = false;
};

class FAssetReferenceWidget
{
public:
    FAssetReferenceEditResult Draw(
        const char* Id,
        const FAssetPath& CurrentValue,
        EAssetReferenceType AllowedType,
        const FAssetRegistry& Registry,
        const FAssetPath& SelectedAsset = {},
        bool bShowUseSelected = true,
        bool bShowBrowse = true);

private:
    std::unordered_map<std::string, std::array<char, 128>> SearchBuffers;
};
}
