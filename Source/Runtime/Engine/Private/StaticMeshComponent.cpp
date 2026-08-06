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
    PICO_ADD_PROPERTY(Properties, StaticMeshAsset);
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
}
