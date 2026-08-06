#pragma once

#include "Pico/Core/AssetPath.h"

#include <vector>

namespace Pico
{
struct FAssetRecord;
class FAssetRegistry;

class FEditorAssetSelection
{
public:
    void Select(const FAssetPath& AssetPath);
    void Add(const FAssetPath& AssetPath);
    void Toggle(const FAssetPath& AssetPath);
    void SetRange(
        const std::vector<FAssetPath>& OrderedAssets,
        const FAssetPath& AssetPath,
        bool bAppend);
    void Clear();

    const FAssetPath& GetSelectedPath() const;
    const std::vector<FAssetPath>& GetSelectedPaths() const;
    bool Contains(const FAssetPath& AssetPath) const;
    std::size_t Num() const;
    const FAssetRecord* Resolve(const FAssetRegistry& Registry) const;
    bool Validate(const FAssetRegistry& Registry);

private:
    std::vector<FAssetPath> SelectedPaths;
    FAssetPath PrimaryPath;
    FAssetPath RangeAnchorPath;
};
}
