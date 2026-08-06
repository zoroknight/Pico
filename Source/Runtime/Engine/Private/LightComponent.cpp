#include "Pico/Engine/LightComponent.h"

#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PLightComponent)

bool PLightComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, bEnabled);
    PICO_ADD_PROPERTY(Properties, LightColor);
    PICO_ADD_PROPERTY(Properties, Intensity);
    return Class.AddProperties(std::move(Properties));
}

PLightComponent::PLightComponent(const FObjectConstructionParams& Params)
    : PSceneComponent(Params)
{
}

bool PLightComponent::IsEnabled() const
{
    return bEnabled;
}

void PLightComponent::SetEnabled(bool bInEnabled)
{
    bEnabled = bInEnabled;
}

const FVector3& PLightComponent::GetLightColor() const
{
    return LightColor;
}

void PLightComponent::SetLightColor(const FVector3& InColor)
{
    LightColor = InColor;
    SanitizeLightParameters();
}

float PLightComponent::GetIntensity() const
{
    return Intensity;
}

void PLightComponent::SetIntensity(float InIntensity)
{
    Intensity = InIntensity;
    SanitizeLightParameters();
}

void PLightComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    PSceneComponent::PostEditChangeProperty(Event);
    SanitizeLightParameters();
}

void PLightComponent::PostLoad()
{
    PSceneComponent::PostLoad();
    SanitizeLightParameters();
}

void PLightComponent::SanitizeLightParameters()
{
    if (!std::isfinite(LightColor.X)) LightColor.X = 1.0f;
    if (!std::isfinite(LightColor.Y)) LightColor.Y = 1.0f;
    if (!std::isfinite(LightColor.Z)) LightColor.Z = 1.0f;
    LightColor.X = std::max(LightColor.X, 0.0f);
    LightColor.Y = std::max(LightColor.Y, 0.0f);
    LightColor.Z = std::max(LightColor.Z, 0.0f);
    if (!std::isfinite(Intensity)) Intensity = 3.0f;
    Intensity = std::max(Intensity, 0.0f);
}
}
