#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/StaticMesh.h"

#include <memory>
#include <vector>

namespace Pico
{
class FAssetManager
{
public:
    std::shared_ptr<const FStaticMeshData> LoadStaticMesh(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        EStaticMeshError* OutError = nullptr);
    std::size_t GetCachedStaticMeshCount() const;
    void Clear();

private:
    struct FStaticMeshCacheEntry
    {
        FAssetPath AssetPath;
        std::uintmax_t FileSize = 0;
        std::filesystem::file_time_type LastWriteTime;
        std::shared_ptr<const FStaticMeshData> Mesh;
    };

    std::vector<FStaticMeshCacheEntry> StaticMeshes;
};
}
