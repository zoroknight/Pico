#include "Pico/Asset/AssetManager.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace Pico
{
std::shared_ptr<const FStaticMeshData> FAssetManager::LoadStaticMesh(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    EStaticMeshError* OutError)
{
    if (OutError != nullptr)
    {
        *OutError = EStaticMeshError::None;
    }
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::StaticMesh)
    {
        if (OutError != nullptr)
        {
            *OutError = EStaticMeshError::InvalidArgument;
        }
        return {};
    }

    const auto Found = std::find_if(
        StaticMeshes.begin(),
        StaticMeshes.end(),
        [Record](const FStaticMeshCacheEntry& Entry)
        {
            return Entry.AssetPath == Record->AssetPath;
        });
    if (Found != StaticMeshes.end()
        && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime)
    {
        return Found->Mesh;
    }

    auto LoadedMesh = std::make_shared<FStaticMeshData>();
    if (!LoadStaticMeshFromFile(Record->FilePath, *LoadedMesh, OutError))
    {
        return {};
    }

    if (Found != StaticMeshes.end())
    {
        Found->FileSize = Record->FileSize;
        Found->LastWriteTime = Record->LastWriteTime;
        Found->Mesh = LoadedMesh;
    }
    else
    {
        StaticMeshes.push_back(FStaticMeshCacheEntry {
            Record->AssetPath,
            Record->FileSize,
            Record->LastWriteTime,
            LoadedMesh
        });
    }
    return LoadedMesh;
}

std::size_t FAssetManager::GetCachedStaticMeshCount() const
{
    return StaticMeshes.size();
}

std::shared_ptr<const FTextureData> FAssetManager::LoadTexture(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    ETextureError* OutError)
{
    if (OutError != nullptr) *OutError = ETextureError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::Texture)
    {
        if (OutError != nullptr) *OutError = ETextureError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(
        Textures.begin(), Textures.end(),
        [Record](const FTextureCacheEntry& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != Textures.end()
        && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime)
    {
        return Found->Texture;
    }
    auto Loaded = std::make_shared<FTextureData>();
    if (!LoadTextureFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != Textures.end())
    {
        Found->FileSize = Record->FileSize;
        Found->LastWriteTime = Record->LastWriteTime;
        Found->Texture = Loaded;
    }
    else
    {
        Textures.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    }
    return Loaded;
}

std::shared_ptr<const FMaterialData> FAssetManager::LoadMaterial(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    EMaterialError* OutError)
{
    if (OutError != nullptr) *OutError = EMaterialError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::Material)
    {
        if (OutError != nullptr) *OutError = EMaterialError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(
        Materials.begin(), Materials.end(),
        [Record](const FMaterialCacheEntry& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != Materials.end()
        && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime)
    {
        return Found->Material;
    }
    auto Loaded = std::make_shared<FMaterialData>();
    if (!LoadMaterialFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != Materials.end())
    {
        Found->FileSize = Record->FileSize;
        Found->LastWriteTime = Record->LastWriteTime;
        Found->Material = Loaded;
    }
    else
    {
        Materials.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    }
    return Loaded;
}

std::size_t FAssetManager::GetCachedTextureCount() const
{
    return Textures.size();
}

std::size_t FAssetManager::GetCachedMaterialCount() const
{
    return Materials.size();
}

std::shared_ptr<const FSkeletonData> FAssetManager::LoadSkeleton(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    ESkeletalAssetError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalAssetError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::Skeleton)
    {
        if (OutError != nullptr) *OutError = ESkeletalAssetError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(Skeletons.begin(), Skeletons.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != Skeletons.end() && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime) return Found->Data;
    auto Loaded = std::make_shared<FSkeletonData>();
    if (!LoadSkeletonFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != Skeletons.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else Skeletons.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::shared_ptr<const FSkeletalMeshData> FAssetManager::LoadSkeletalMesh(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    ESkeletalAssetError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalAssetError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::SkeletalMesh)
    {
        if (OutError != nullptr) *OutError = ESkeletalAssetError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(SkeletalMeshes.begin(), SkeletalMeshes.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != SkeletalMeshes.end() && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime) return Found->Data;
    auto Loaded = std::make_shared<FSkeletalMeshData>();
    if (!LoadSkeletalMeshFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != SkeletalMeshes.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else SkeletalMeshes.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::shared_ptr<const FAnimationClipData> FAssetManager::LoadAnimationClip(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    ESkeletalAssetError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalAssetError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::AnimationClip)
    {
        if (OutError != nullptr) *OutError = ESkeletalAssetError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(AnimationClips.begin(), AnimationClips.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != AnimationClips.end() && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime) return Found->Data;
    auto Loaded = std::make_shared<FAnimationClipData>();
    if (!LoadAnimationClipFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != AnimationClips.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else AnimationClips.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::shared_ptr<const FAnimationSetData> FAssetManager::LoadAnimationSet(
    const FAssetPath& AssetPath, const FAssetRegistry& Registry, ESkeletalAssetError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalAssetError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::AnimationSet)
    { if (OutError != nullptr) *OutError = ESkeletalAssetError::InvalidArgument; return {}; }
    auto Found = std::find_if(AnimationSets.begin(), AnimationSets.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != AnimationSets.end() && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime) return Found->Data;
    auto Loaded = std::make_shared<FAnimationSetData>();
    if (!LoadAnimationSetFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != AnimationSets.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else AnimationSets.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::shared_ptr<const FAnimationMontageData> FAssetManager::LoadAnimationMontage(
    const FAssetPath& AssetPath, const FAssetRegistry& Registry, ESkeletalAssetError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalAssetError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::AnimationMontage)
    { if (OutError != nullptr) *OutError = ESkeletalAssetError::InvalidArgument; return {}; }
    auto Found = std::find_if(AnimationMontages.begin(), AnimationMontages.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != AnimationMontages.end() && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime) return Found->Data;
    auto Loaded = std::make_shared<FAnimationMontageData>();
    if (!LoadAnimationMontageFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != AnimationMontages.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else AnimationMontages.push_back({Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::shared_ptr<const FCharacterProfileData> FAssetManager::LoadCharacterProfile(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    ECharacterProfileError* OutError)
{
    if (OutError != nullptr) *OutError = ECharacterProfileError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::CharacterProfile)
    {
        if (OutError != nullptr) *OutError = ECharacterProfileError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(CharacterProfiles.begin(), CharacterProfiles.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != CharacterProfiles.end() && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime) return Found->Data;
    auto Loaded = std::make_shared<FCharacterProfileData>();
    if (!LoadCharacterProfileFromFile(Record->FilePath, *Loaded, OutError)) return {};
    if (Found != CharacterProfiles.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else CharacterProfiles.push_back(
        {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::shared_ptr<const FThirdPersonControlProfileData>
FAssetManager::LoadThirdPersonControlProfile(
    const FAssetPath& AssetPath,
    const FAssetRegistry& Registry,
    EThirdPersonControlProfileError* OutError)
{
    if (OutError != nullptr) *OutError = EThirdPersonControlProfileError::None;
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr || Record->Type != EAssetType::ThirdPersonControlProfile)
    {
        if (OutError != nullptr)
            *OutError = EThirdPersonControlProfileError::InvalidArgument;
        return {};
    }
    auto Found = std::find_if(
        ThirdPersonControlProfiles.begin(), ThirdPersonControlProfiles.end(),
        [Record](const auto& Entry) { return Entry.AssetPath == Record->AssetPath; });
    if (Found != ThirdPersonControlProfiles.end()
        && Found->FileSize == Record->FileSize
        && Found->LastWriteTime == Record->LastWriteTime)
    {
        return Found->Data;
    }
    auto Loaded = std::make_shared<FThirdPersonControlProfileData>();
    if (!LoadThirdPersonControlProfileFromFile(Record->FilePath, *Loaded, OutError))
        return {};
    if (Found != ThirdPersonControlProfiles.end())
        *Found = {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded};
    else
        ThirdPersonControlProfiles.push_back(
            {Record->AssetPath, Record->FileSize, Record->LastWriteTime, Loaded});
    return Loaded;
}

std::size_t FAssetManager::GetCachedSkeletonCount() const { return Skeletons.size(); }
std::size_t FAssetManager::GetCachedSkeletalMeshCount() const { return SkeletalMeshes.size(); }
std::size_t FAssetManager::GetCachedAnimationClipCount() const { return AnimationClips.size(); }
std::size_t FAssetManager::GetCachedAnimationSetCount() const { return AnimationSets.size(); }
std::size_t FAssetManager::GetCachedAnimationMontageCount() const { return AnimationMontages.size(); }
std::size_t FAssetManager::GetCachedCharacterProfileCount() const { return CharacterProfiles.size(); }
std::size_t FAssetManager::GetCachedThirdPersonControlProfileCount() const
{ return ThirdPersonControlProfiles.size(); }

void FAssetManager::Invalidate(const FAssetPath& AssetPath)
{
    const auto EqualsIgnoringCase = [](std::string_view Left, std::string_view Right)
    {
        return Left.size() == Right.size()
            && std::equal(
                Left.begin(), Left.end(), Right.begin(),
                [](unsigned char A, unsigned char B)
                {
                    return std::tolower(A) == std::tolower(B);
                });
    };
    std::erase_if(
        StaticMeshes,
        [&AssetPath, &EqualsIgnoringCase](const FStaticMeshCacheEntry& Entry)
        {
            return EqualsIgnoringCase(
                Entry.AssetPath.ToString(), AssetPath.ToString());
        });
    std::erase_if(
        Textures,
        [&AssetPath, &EqualsIgnoringCase](const FTextureCacheEntry& Entry)
        {
            return EqualsIgnoringCase(Entry.AssetPath.ToString(), AssetPath.ToString());
        });
    std::erase_if(
        Materials,
        [&AssetPath, &EqualsIgnoringCase](const FMaterialCacheEntry& Entry)
        {
            return EqualsIgnoringCase(Entry.AssetPath.ToString(), AssetPath.ToString());
        });
    const auto EraseAnimation = [&AssetPath, &EqualsIgnoringCase](auto& Cache)
    {
        std::erase_if(Cache, [&AssetPath, &EqualsIgnoringCase](const auto& Entry)
        {
            return EqualsIgnoringCase(Entry.AssetPath.ToString(), AssetPath.ToString());
        });
    };
    EraseAnimation(Skeletons);
    EraseAnimation(SkeletalMeshes);
    EraseAnimation(AnimationClips);
    EraseAnimation(AnimationSets);
    EraseAnimation(AnimationMontages);
    EraseAnimation(CharacterProfiles);
    EraseAnimation(ThirdPersonControlProfiles);
}

void FAssetManager::Clear()
{
    StaticMeshes.clear();
    Textures.clear();
    Materials.clear();
    Skeletons.clear();
    SkeletalMeshes.clear();
    AnimationClips.clear();
    AnimationSets.clear();
    AnimationMontages.clear();
    CharacterProfiles.clear();
    ThirdPersonControlProfiles.clear();
}
}
