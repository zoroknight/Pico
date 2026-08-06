#pragma once

#include "Pico/Engine/SceneComponent.h"

namespace Pico
{
class PCameraComponent final : public PSceneComponent
{
    PICO_DECLARE_CLASS(PCameraComponent, PSceneComponent)

public:
    bool IsActive() const;
    void SetActive(bool bInActive);
    float GetVerticalFieldOfViewDegrees() const;
    void SetVerticalFieldOfViewDegrees(float InDegrees);
    float GetNearPlane() const;
    void SetNearPlane(float InNearPlane);
    float GetFarPlane() const;
    void SetFarPlane(float InFarPlane);
    FVector3 GetViewPosition() const;
    FVector3 GetViewForward() const;
    FVector3 GetViewUp() const;
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;

protected:
    explicit PCameraComponent(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    void SanitizeParameters();

    bool bActive = true;
    float VerticalFieldOfViewDegrees = 50.0f;
    float NearPlane = 1.0f;
    float FarPlane = 10000.0f;
};
}
