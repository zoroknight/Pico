#include "Pico/Engine/PrimitiveComponent.h"

#include "Pico/Object/Class.h"

#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PPrimitiveComponent)

bool PPrimitiveComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, bVisible);
    PICO_ADD_PROPERTY(Properties, Color);
    return Class.AddProperties(std::move(Properties));
}

PPrimitiveComponent::PPrimitiveComponent(const FObjectConstructionParams& Params)
    : PSceneComponent(Params)
{
}

bool PPrimitiveComponent::IsVisible() const
{
    return bVisible;
}

void PPrimitiveComponent::SetVisible(bool bInVisible)
{
    bVisible = bInVisible;
}

const FVector3& PPrimitiveComponent::GetColor() const
{
    return Color;
}

void PPrimitiveComponent::SetColor(const FVector3& InColor)
{
    Color = InColor;
}
}
