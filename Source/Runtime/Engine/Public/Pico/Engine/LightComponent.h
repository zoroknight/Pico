#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Engine/SceneComponent.h"

namespace Pico
{
class PLightComponent : public PSceneComponent
{
    PICO_DECLARE_CLASS(PLightComponent, PSceneComponent)

public:
    bool IsEnabled() const;
    void SetEnabled(bool bInEnabled);
    const FVector3& GetLightColor() const;
    void SetLightColor(const FVector3& InColor);
    float GetIntensity() const;
    void SetIntensity(float InIntensity);
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;

protected:
    explicit PLightComponent(const FObjectConstructionParams& Params);
    void PostLoad() override;
    void SanitizeLightParameters();

private:
    bool bEnabled = true;
    FVector3 LightColor = FVector3::OneVector;
    float Intensity = 3.0f;
};
}
