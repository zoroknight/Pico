#pragma once

#include "Pico/Core/AssetPath.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Pico
{
enum class EAssetType
{
    World,
    StaticMesh,
    Texture,
    Material,
    Skeleton,
    SkeletalMesh,
    AnimationClip
};

enum class EAssetScanError
{
    ContentUnavailable,
    TraversalFailed,
    InvalidAssetPath,
    DuplicateAssetPath
};

struct FAssetRecord
{
    FAssetPath AssetPath;
    std::filesystem::path FilePath;
    EAssetType Type = EAssetType::World;
    std::uintmax_t FileSize = 0;
    std::filesystem::file_time_type LastWriteTime;
};

struct FAssetScanIssue
{
    EAssetScanError Error = EAssetScanError::TraversalFailed;
    std::filesystem::path FilePath;
};

struct FAssetScanReport
{
    std::size_t ScannedFileCount = 0;
    std::size_t RegisteredAssetCount = 0;
    std::size_t IgnoredFileCount = 0;
    std::vector<FAssetScanIssue> Issues;
};

class FAssetRegistry
{
public:
    bool ScanProjectContent(FAssetScanReport* OutReport = nullptr);
    const FAssetRecord* Find(const FAssetPath& AssetPath) const;
    std::span<const FAssetRecord> GetAssets() const;
    void Clear();

private:
    std::vector<FAssetRecord> Assets;
    std::vector<std::pair<std::string, std::size_t>> Lookup;
};

std::string_view ToString(EAssetType Type);
std::string_view ToString(EAssetScanError Error);
}
