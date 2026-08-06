#include "Pico/Editor/EditorAssetSelection.h"

#include "Pico/Asset/AssetRegistry.h"

#include <algorithm>
#include <iterator>

namespace Pico
{
void FEditorAssetSelection::Select(const FAssetPath& AssetPath)
{
    SelectedPaths.clear();
    if (AssetPath.IsValid())
    {
        SelectedPaths.push_back(AssetPath);
    }
    PrimaryPath = AssetPath;
    RangeAnchorPath = AssetPath;
}

void FEditorAssetSelection::Add(const FAssetPath& AssetPath)
{
    if (!AssetPath.IsValid() || Contains(AssetPath))
    {
        return;
    }
    SelectedPaths.push_back(AssetPath);
    PrimaryPath = AssetPath;
    RangeAnchorPath = AssetPath;
}

void FEditorAssetSelection::Toggle(const FAssetPath& AssetPath)
{
    const auto Found = std::find(SelectedPaths.begin(), SelectedPaths.end(), AssetPath);
    if (Found == SelectedPaths.end())
    {
        Add(AssetPath);
        return;
    }
    SelectedPaths.erase(Found);
    if (PrimaryPath == AssetPath)
    {
        PrimaryPath = SelectedPaths.empty() ? FAssetPath {} : SelectedPaths.back();
    }
    RangeAnchorPath = AssetPath;
}

void FEditorAssetSelection::SetRange(
    const std::vector<FAssetPath>& OrderedAssets,
    const FAssetPath& AssetPath,
    bool bAppend)
{
    auto Anchor = std::find(OrderedAssets.begin(), OrderedAssets.end(), RangeAnchorPath);
    const auto Target = std::find(OrderedAssets.begin(), OrderedAssets.end(), AssetPath);
    if (Target == OrderedAssets.end())
    {
        return;
    }
    if (Anchor == OrderedAssets.end())
    {
        Anchor = Target;
    }
    if (!bAppend)
    {
        SelectedPaths.clear();
    }
    const auto First = std::min(Anchor, Target);
    const auto Last = std::max(Anchor, Target);
    for (auto It = First; It != std::next(Last); ++It)
    {
        if (!Contains(*It))
        {
            SelectedPaths.push_back(*It);
        }
    }
    PrimaryPath = AssetPath;
}

void FEditorAssetSelection::Clear()
{
    SelectedPaths.clear();
    PrimaryPath = {};
    RangeAnchorPath = {};
}

const FAssetPath& FEditorAssetSelection::GetSelectedPath() const
{
    return PrimaryPath;
}

const std::vector<FAssetPath>& FEditorAssetSelection::GetSelectedPaths() const
{
    return SelectedPaths;
}

bool FEditorAssetSelection::Contains(const FAssetPath& AssetPath) const
{
    return std::find(SelectedPaths.begin(), SelectedPaths.end(), AssetPath)
        != SelectedPaths.end();
}

std::size_t FEditorAssetSelection::Num() const
{
    return SelectedPaths.size();
}

const FAssetRecord* FEditorAssetSelection::Resolve(const FAssetRegistry& Registry) const
{
    return PrimaryPath.IsValid() ? Registry.Find(PrimaryPath) : nullptr;
}

bool FEditorAssetSelection::Validate(const FAssetRegistry& Registry)
{
    const std::size_t PreviousCount = SelectedPaths.size();
    std::erase_if(
        SelectedPaths,
        [&Registry](const FAssetPath& Path) { return Registry.Find(Path) == nullptr; });
    if (Registry.Find(PrimaryPath) == nullptr)
    {
        PrimaryPath = SelectedPaths.empty() ? FAssetPath {} : SelectedPaths.back();
    }
    if (Registry.Find(RangeAnchorPath) == nullptr)
    {
        RangeAnchorPath = PrimaryPath;
    }
    return PreviousCount == SelectedPaths.size();
}
}
