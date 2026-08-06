#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Asset/Texture.h"

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
    std::shared_ptr<const FTextureData> LoadTexture(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ETextureError* OutError = nullptr);
    std::shared_ptr<const FMaterialData> LoadMaterial(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        EMaterialError* OutError = nullptr);
    std::size_t GetCachedStaticMeshCount() const;
    std::size_t GetCachedTextureCount() const;
    std::size_t GetCachedMaterialCount() const;
    void Invalidate(const FAssetPath& AssetPath);
    void Clear();

private:
    struct FStaticMeshCacheEntry
    {
        FAssetPath AssetPath;
        std::uintmax_t FileSize = 0;
        std::filesystem::file_time_type LastWriteTime;
        std::shared_ptr<const FStaticMeshData> Mesh;
    };

    struct FTextureCacheEntry
    {
        FAssetPath AssetPath;
        std::uintmax_t FileSize = 0;
        std::filesystem::file_time_type LastWriteTime;
        std::shared_ptr<const FTextureData> Texture;
    };

    struct FMaterialCacheEntry
    {
        FAssetPath AssetPath;
        std::uintmax_t FileSize = 0;
        std::filesystem::file_time_type LastWriteTime;
        std::shared_ptr<const FMaterialData> Material;
    };

    std::vector<FStaticMeshCacheEntry> StaticMeshes;
    std::vector<FTextureCacheEntry> Textures;
    std::vector<FMaterialCacheEntry> Materials;
};
}
