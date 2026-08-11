#include "Pico/Engine/CapsuleComponent.h"

#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PCapsuleComponent)

bool PCapsuleComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Radius);
    PICO_ADD_PROPERTY(Properties, HalfHeight);
    return Class.AddProperties(std::move(Properties));
}

PCapsuleComponent::PCapsuleComponent(const FObjectConstructionParams& Params)
    : PPrimitiveComponent(Params)
{
}

float PCapsuleComponent::GetRadius() const { return Radius; }
void PCapsuleComponent::SetRadius(float InRadius)
{
    Radius = std::isfinite(InRadius) ? std::max(InRadius, 1.0f) : 42.0f;
    HalfHeight = std::max(HalfHeight, Radius);
    RecreatePhysicsState();
}

float PCapsuleComponent::GetHalfHeight() const { return HalfHeight; }
void PCapsuleComponent::SetHalfHeight(float InHalfHeight)
{
    HalfHeight = std::isfinite(InHalfHeight)
        ? std::max(InHalfHeight, Radius)
        : std::max(96.0f, Radius);
    RecreatePhysicsState();
}

FCollisionShape PCapsuleComponent::GetCollisionShape() const
{
    const FVector3 Scale = GetWorldTransform().Scale;
    const float RadiusScale = std::max(std::abs(Scale.X), std::abs(Scale.Y));
    return FCollisionShape::MakeCapsule(
        Radius * RadiusScale,
        std::max(HalfHeight * std::abs(Scale.Z), Radius * RadiusScale));
}
}
