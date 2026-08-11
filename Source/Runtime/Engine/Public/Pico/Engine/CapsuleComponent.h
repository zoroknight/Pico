#pragma once

#include "Pico/Engine/PrimitiveComponent.h"

namespace Pico
{
class PCapsuleComponent final : public PPrimitiveComponent
{
    PICO_DECLARE_CLASS(PCapsuleComponent, PPrimitiveComponent)

public:
    float GetRadius() const;
    void SetRadius(float InRadius);
    float GetHalfHeight() const;
    void SetHalfHeight(float InHalfHeight);
    FCollisionShape GetCollisionShape() const override;

protected:
    explicit PCapsuleComponent(const FObjectConstructionParams& Params);

private:
    float Radius = 42.0f;
    float HalfHeight = 96.0f;
};
}
