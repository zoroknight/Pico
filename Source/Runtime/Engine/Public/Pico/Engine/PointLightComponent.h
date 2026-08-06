#pragma once

#include "Pico/Engine/LightComponent.h"

namespace Pico
{
class PPointLightComponent final : public PLightComponent
{
    PICO_DECLARE_CLASS(PPointLightComponent, PLightComponent)

public:
    float GetAttenuationRadius() const;
    void SetAttenuationRadius(float InRadius);
    FVector3 GetLightPosition() const;
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;

protected:
    explicit PPointLightComponent(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    void SanitizePointLightParameters();

    float AttenuationRadius = 500.0f;
};
}
