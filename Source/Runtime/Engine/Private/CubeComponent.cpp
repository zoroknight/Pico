#include "Pico/Engine/CubeComponent.h"

#include "Pico/Object/Class.h"

#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PCubeComponent)

bool PCubeComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Extent);
    return Class.AddProperties(std::move(Properties));
}

PCubeComponent::PCubeComponent(const FObjectConstructionParams& Params)
    : PPrimitiveComponent(Params)
{
}

const FVector3& PCubeComponent::GetExtent() const
{
    return Extent;
}

void PCubeComponent::SetExtent(const FVector3& InExtent)
{
    Extent = InExtent;
}
}
