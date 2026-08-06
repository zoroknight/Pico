#include "Pico/Engine/StaticMeshComponent.h"

#include "Pico/Object/Class.h"

#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PStaticMeshComponent)

bool PStaticMeshComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_ASSET_PROPERTY(Properties, StaticMeshAsset, StaticMesh);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialAsset, Material);
    return Class.AddProperties(std::move(Properties));
}

PStaticMeshComponent::PStaticMeshComponent(
    const FObjectConstructionParams& Params)
    : PPrimitiveComponent(Params)
{
}

const FAssetPath& PStaticMeshComponent::GetStaticMeshAsset() const
{
    return StaticMeshAsset;
}

void PStaticMeshComponent::SetStaticMeshAsset(const FAssetPath& InAssetPath)
{
    StaticMeshAsset = InAssetPath;
}

const FAssetPath& PStaticMeshComponent::GetMaterialAsset() const
{
    return MaterialAsset;
}

void PStaticMeshComponent::SetMaterialAsset(const FAssetPath& InAssetPath)
{
    MaterialAsset = InAssetPath;
}
}
