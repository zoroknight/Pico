#pragma once

#include "Pico/Engine/PawnMovementComponent.h"

namespace Pico
{
class PFloatingPawnMovement : public PPawnMovementComponent
{
    PICO_DECLARE_CLASS(PFloatingPawnMovement, PPawnMovementComponent)

public:
    float GetMaxSpeed() const;
    void SetMaxSpeed(float Value);
    float GetAcceleration() const;
    void SetAcceleration(float Value);
    float GetDeceleration() const;
    void SetDeceleration(float Value);
    void TickComponent(float DeltaSeconds) override;

protected:
    explicit PFloatingPawnMovement(const FObjectConstructionParams& Params);

private:
    float MaxSpeed = 250.0f;
    float Acceleration = 1000.0f;
    float Deceleration = 1500.0f;
};
}
