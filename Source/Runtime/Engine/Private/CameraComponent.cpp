#include "Pico/Engine/CameraComponent.h"

#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PCameraComponent)

bool PCameraComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, bActive);
    PICO_ADD_PROPERTY(Properties, VerticalFieldOfViewDegrees);
    PICO_ADD_PROPERTY(Properties, NearPlane);
    PICO_ADD_PROPERTY(Properties, FarPlane);
    return Class.AddProperties(std::move(Properties));
}

PCameraComponent::PCameraComponent(const FObjectConstructionParams& Params)
    : PSceneComponent(Params)
{
}

bool PCameraComponent::IsActive() const
{
    return bActive;
}

void PCameraComponent::SetActive(bool bInActive)
{
    bActive = bInActive;
}

float PCameraComponent::GetVerticalFieldOfViewDegrees() const
{
    return VerticalFieldOfViewDegrees;
}

void PCameraComponent::SetVerticalFieldOfViewDegrees(float InDegrees)
{
    VerticalFieldOfViewDegrees = InDegrees;
    SanitizeParameters();
}

float PCameraComponent::GetNearPlane() const
{
    return NearPlane;
}

void PCameraComponent::SetNearPlane(float InNearPlane)
{
    NearPlane = InNearPlane;
    SanitizeParameters();
}

float PCameraComponent::GetFarPlane() const
{
    return FarPlane;
}

void PCameraComponent::SetFarPlane(float InFarPlane)
{
    FarPlane = InFarPlane;
    SanitizeParameters();
}

FVector3 PCameraComponent::GetViewPosition() const
{
    return GetWorldTransform().Translation;
}

FVector3 PCameraComponent::GetViewForward() const
{
    return GetWorldTransform().Rotation.RotateVector(
        FVector3::ForwardVector).GetSafeNormal();
}

FVector3 PCameraComponent::GetViewUp() const
{
    return GetWorldTransform().Rotation.RotateVector(
        FVector3::UpVector).GetSafeNormal();
}

void PCameraComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    PSceneComponent::PostEditChangeProperty(Event);
    SanitizeParameters();
}

void PCameraComponent::PostLoad()
{
    PSceneComponent::PostLoad();
    SanitizeParameters();
}

void PCameraComponent::SanitizeParameters()
{
    if (!std::isfinite(VerticalFieldOfViewDegrees))
    {
        VerticalFieldOfViewDegrees = 50.0f;
    }
    if (!std::isfinite(NearPlane))
    {
        NearPlane = 1.0f;
    }
    if (!std::isfinite(FarPlane))
    {
        FarPlane = 10000.0f;
    }
    VerticalFieldOfViewDegrees = std::clamp(
        VerticalFieldOfViewDegrees, 5.0f, 170.0f);
    NearPlane = std::max(NearPlane, 0.01f);
    FarPlane = std::max(FarPlane, NearPlane + 0.01f);
}
}
