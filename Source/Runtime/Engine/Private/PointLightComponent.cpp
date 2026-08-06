#include "Pico/Engine/PointLightComponent.h"

#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PPointLightComponent)

bool PPointLightComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, AttenuationRadius);
    return Class.AddProperties(std::move(Properties));
}

PPointLightComponent::PPointLightComponent(
    const FObjectConstructionParams& Params)
    : PLightComponent(Params)
{
}

float PPointLightComponent::GetAttenuationRadius() const
{
    return AttenuationRadius;
}

void PPointLightComponent::SetAttenuationRadius(float InRadius)
{
    AttenuationRadius = InRadius;
    SanitizePointLightParameters();
}

FVector3 PPointLightComponent::GetLightPosition() const
{
    return GetWorldTransform().Translation;
}

void PPointLightComponent::PostEditChangeProperty(
    const FPropertyChangedEvent& Event)
{
    PLightComponent::PostEditChangeProperty(Event);
    SanitizePointLightParameters();
}

void PPointLightComponent::PostLoad()
{
    PLightComponent::PostLoad();
    SanitizePointLightParameters();
}

void PPointLightComponent::SanitizePointLightParameters()
{
    if (!std::isfinite(AttenuationRadius)) AttenuationRadius = 500.0f;
    AttenuationRadius = std::max(AttenuationRadius, 0.01f);
}
}
