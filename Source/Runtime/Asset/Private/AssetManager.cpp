#include "Pico/Asset/AssetManager.h"

#include <algorithm>
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
        [&AssetPath](const FStaticMeshCacheEntry& Entry)
        {
            return Entry.AssetPath == AssetPath;
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
            AssetPath,
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

void FAssetManager::Clear()
{
    StaticMeshes.clear();
}
}
