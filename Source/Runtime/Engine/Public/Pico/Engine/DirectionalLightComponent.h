#pragma once

#include "Pico/Engine/LightComponent.h"

namespace Pico
{
class PDirectionalLightComponent final : public PLightComponent
{
    PICO_DECLARE_CLASS(PDirectionalLightComponent, PLightComponent)

public:
    FVector3 GetLightDirection() const;

protected:
    explicit PDirectionalLightComponent(const FObjectConstructionParams& Params);
};
}
