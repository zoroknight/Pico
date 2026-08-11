#include "Pico/Engine/FloatingPawnMovement.h"

#include "Pico/Engine/SceneComponent.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PFloatingPawnMovement)

namespace
{
FVector3 MoveToward(
    const FVector3& Current,
    const FVector3& Target,
    float MaxDelta)
{
    const FVector3 Difference = Target - Current;
    const float Distance = Difference.Size();
    if (Distance <= MaxDelta || Distance <= SmallNumber)
    {
        return Target;
    }
    return Current + Difference * (MaxDelta / Distance);
}
}

bool PFloatingPawnMovement::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, MaxSpeed);
    PICO_ADD_PROPERTY(Properties, Acceleration);
    PICO_ADD_PROPERTY(Properties, Deceleration);
    return Class.AddProperties(std::move(Properties));
}

PFloatingPawnMovement::PFloatingPawnMovement(
    const FObjectConstructionParams& Params)
    : PPawnMovementComponent(Params)
{
}

float PFloatingPawnMovement::GetMaxSpeed() const { return MaxSpeed; }
void PFloatingPawnMovement::SetMaxSpeed(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) MaxSpeed = Value;
}
float PFloatingPawnMovement::GetAcceleration() const { return Acceleration; }
void PFloatingPawnMovement::SetAcceleration(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) Acceleration = Value;
}
float PFloatingPawnMovement::GetDeceleration() const { return Deceleration; }
void PFloatingPawnMovement::SetDeceleration(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) Deceleration = Value;
}

void PFloatingPawnMovement::TickComponent(float DeltaSeconds)
{
    PPawnMovementComponent::TickComponent(DeltaSeconds);
    PSceneComponent* Component = GetUpdatedComponent();
    if (Component == nullptr || !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
    {
        return;
    }

    FVector3 Input = ConsumeInputVector();
    const float InputSize = Input.Size();
    if (InputSize > 1.0f)
    {
        Input /= InputSize;
    }

    FVector3 NewVelocity = GetVelocity();
    if (!Input.IsNearlyZero())
    {
        NewVelocity = MoveToward(
            NewVelocity,
            Input * MaxSpeed,
            Acceleration * DeltaSeconds);
    }
    else
    {
        NewVelocity = MoveToward(
            NewVelocity,
            FVector3::ZeroVector,
            Deceleration * DeltaSeconds);
    }
    if (MaxSpeed <= 0.0f)
    {
        NewVelocity = FVector3::ZeroVector;
    }
    else if (NewVelocity.Size() > MaxSpeed)
    {
        NewVelocity = NewVelocity.GetSafeNormal() * MaxSpeed;
    }
    SetVelocity(NewVelocity);

    const FVector3 Delta = NewVelocity * DeltaSeconds;
    FHitResult Hit;
    SafeMoveUpdatedComponent(
        Delta,
        Component->GetWorldTransform().Rotation,
        true,
        &Hit);
    if (Hit.bBlockingHit)
    {
        const FVector3 BlockingNormal = Hit.Normal.GetSafeNormal();
        SlideAlongSurface(
            Delta,
            1.0f - Hit.Time,
            Hit.Normal,
            Hit,
            true);
        SetVelocity(NewVelocity
            - BlockingNormal * FVector3::Dot(NewVelocity, BlockingNormal));
    }
}
}
