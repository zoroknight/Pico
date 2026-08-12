#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/CharacterProfile.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/SkeletalAnimation.h"
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
    std::shared_ptr<const FSkeletonData> LoadSkeleton(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ESkeletalAssetError* OutError = nullptr);
    std::shared_ptr<const FSkeletalMeshData> LoadSkeletalMesh(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ESkeletalAssetError* OutError = nullptr);
    std::shared_ptr<const FAnimationClipData> LoadAnimationClip(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ESkeletalAssetError* OutError = nullptr);
    std::shared_ptr<const FAnimationSetData> LoadAnimationSet(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ESkeletalAssetError* OutError = nullptr);
    std::shared_ptr<const FAnimationMontageData> LoadAnimationMontage(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ESkeletalAssetError* OutError = nullptr);
    std::shared_ptr<const FCharacterProfileData> LoadCharacterProfile(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry,
        ECharacterProfileError* OutError = nullptr);
    std::size_t GetCachedStaticMeshCount() const;
    std::size_t GetCachedTextureCount() const;
    std::size_t GetCachedMaterialCount() const;
    std::size_t GetCachedSkeletonCount() const;
    std::size_t GetCachedSkeletalMeshCount() const;
    std::size_t GetCachedAnimationClipCount() const;
    std::size_t GetCachedAnimationSetCount() const;
    std::size_t GetCachedAnimationMontageCount() const;
    std::size_t GetCachedCharacterProfileCount() const;
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

    template<typename DataType>
    struct TAnimationCacheEntry
    {
        FAssetPath AssetPath;
        std::uintmax_t FileSize = 0;
        std::filesystem::file_time_type LastWriteTime;
        std::shared_ptr<const DataType> Data;
    };

    std::vector<FStaticMeshCacheEntry> StaticMeshes;
    std::vector<FTextureCacheEntry> Textures;
    std::vector<FMaterialCacheEntry> Materials;
    std::vector<TAnimationCacheEntry<FSkeletonData>> Skeletons;
    std::vector<TAnimationCacheEntry<FSkeletalMeshData>> SkeletalMeshes;
    std::vector<TAnimationCacheEntry<FAnimationClipData>> AnimationClips;
    std::vector<TAnimationCacheEntry<FAnimationSetData>> AnimationSets;
    std::vector<TAnimationCacheEntry<FAnimationMontageData>> AnimationMontages;
    std::vector<TAnimationCacheEntry<FCharacterProfileData>> CharacterProfiles;
};
}
