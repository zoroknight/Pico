#include "Pico/Engine/SceneComponent.h"

#include "Pico/Object/Class.h"

#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PSceneComponent)

bool PSceneComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, RelativeTransform);
    return Class.AddProperties(std::move(Properties));
}

PSceneComponent::PSceneComponent(const FObjectConstructionParams& Params)
    : PActorComponent(Params)
{
}

const FTransform& PSceneComponent::GetRelativeTransform() const
{
    return RelativeTransform;
}

void PSceneComponent::SetRelativeTransform(const FTransform& Transform)
{
    RelativeTransform = Transform;
}

const FVector3& PSceneComponent::GetRelativeLocation() const
{
    return RelativeTransform.Translation;
}

void PSceneComponent::SetRelativeLocation(const FVector3& Location)
{
    RelativeTransform.Translation = Location;
}

FRotator PSceneComponent::GetRelativeRotation() const
{
    return RelativeTransform.Rotation.Rotator();
}

void PSceneComponent::SetRelativeRotation(const FRotator& Rotation)
{
    RelativeTransform.Rotation = Rotation.Quaternion();
}

const FVector3& PSceneComponent::GetRelativeScale() const
{
    return RelativeTransform.Scale;
}

void PSceneComponent::SetRelativeScale(const FVector3& Scale)
{
    RelativeTransform.Scale = Scale;
}

FTransform PSceneComponent::GetWorldTransform() const
{
    return RelativeTransform;
}

void PSceneComponent::SetWorldTransform(const FTransform& Transform)
{
    // Until an attachment tree exists, a scene component's relative space is world space.
    RelativeTransform = Transform;
}
}
