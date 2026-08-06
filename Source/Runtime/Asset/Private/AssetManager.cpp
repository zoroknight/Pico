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
}

void FAssetManager::Clear()
{
    StaticMeshes.clear();
    Textures.clear();
    Materials.clear();
}
}
