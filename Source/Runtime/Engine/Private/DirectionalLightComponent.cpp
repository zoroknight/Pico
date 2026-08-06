#include "Pico/Engine/DirectionalLightComponent.h"

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PDirectionalLightComponent)

PDirectionalLightComponent::PDirectionalLightComponent(
    const FObjectConstructionParams& Params)
    : PLightComponent(Params)
{
}

FVector3 PDirectionalLightComponent::GetLightDirection() const
{
    return GetWorldTransform().Rotation.RotateVector(
        FVector3::ForwardVector).GetSafeNormal();
}
}
