#include "Pico/Engine/MovementComponent.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/SceneComponent.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PMovementComponent)

PMovementComponent::PMovementComponent(const FObjectConstructionParams& Params)
    : PActorComponent(Params)
{
    PrimaryComponentTick.SetCanEverTick(true);
    PrimaryComponentTick.SetTickGroup(ETickGroup::PrePhysics);
}

PSceneComponent* PMovementComponent::GetUpdatedComponent() const
{
    return UpdatedComponent.Get();
}

bool PMovementComponent::SetUpdatedComponent(PSceneComponent* Component)
{
    if (!CheckGameThread("PMovementComponent::SetUpdatedComponent")
        || (Component != nullptr && Component->GetOwner() != GetOwner()))
    {
        return false;
    }
    UpdatedComponent = Component;
    return true;
}

const FVector3& PMovementComponent::GetVelocity() const
{
    return Velocity;
}

void PMovementComponent::SetVelocity(const FVector3& InVelocity)
{
    Velocity = std::isfinite(InVelocity.X)
            && std::isfinite(InVelocity.Y)
            && std::isfinite(InVelocity.Z)
        ? InVelocity
        : FVector3::ZeroVector;
}

const FVector3& PMovementComponent::GetLastMoveDelta() const
{
    return LastMoveDelta;
}

const FHitResult& PMovementComponent::GetLastHitResult() const
{
    return LastHitResult;
}

bool PMovementComponent::WasLastMoveSwept() const
{
    return bLastMoveSwept;
}

ETeleportType PMovementComponent::GetLastTeleportType() const
{
    return LastTeleportType;
}

bool PMovementComponent::MoveUpdatedComponent(
    const FVector3& Delta,
    const FQuat& NewRotation,
    bool bSweep,
    FHitResult* OutHit,
    ETeleportType Teleport)
{
    PSceneComponent* Component = GetUpdatedComponent();
    if (Component == nullptr)
    {
        return false;
    }

    FHitResult Hit;
    const bool bMoved = Component->MoveComponent(
        Delta,
        NewRotation,
        bSweep,
        &Hit,
        EMoveComponentFlags::None,
        Teleport);
    LastMoveDelta = Component->GetWorldTransform().Translation - Hit.TraceStart;
    LastHitResult = Hit;
    bLastMoveSwept = bSweep && Teleport == ETeleportType::None;
    LastTeleportType = Teleport;
    if (OutHit != nullptr)
    {
        *OutHit = Hit;
    }
    return bMoved;
}

bool PMovementComponent::SafeMoveUpdatedComponent(
    const FVector3& Delta,
    const FQuat& NewRotation,
    bool bSweep,
    FHitResult* OutHit,
    ETeleportType Teleport)
{
    FHitResult Hit;
    bool bMoved = MoveUpdatedComponent(
        Delta, NewRotation, bSweep, &Hit, Teleport);
    if (bSweep && Hit.bStartPenetrating && !Hit.Normal.IsNearlyZero())
    {
        const FVector3 Adjustment = Hit.Normal.GetSafeNormal()
            * std::max(Hit.PenetrationDepth + 0.125f, 0.125f);
        FHitResult AdjustmentHit;
        MoveUpdatedComponent(
            Adjustment,
            NewRotation,
            false,
            &AdjustmentHit,
            ETeleportType::TeleportPhysics);
        bMoved = MoveUpdatedComponent(
            Delta, NewRotation, true, &Hit, ETeleportType::None);
    }
    if (OutHit != nullptr)
    {
        *OutHit = Hit;
    }
    return bMoved;
}

float PMovementComponent::SlideAlongSurface(
    const FVector3& Delta,
    float Time,
    const FVector3& Normal,
    FHitResult& Hit,
    bool bSweep)
{
    const float RemainingTime = std::clamp(Time, 0.0f, 1.0f);
    const FVector3 UnitNormal = Normal.GetSafeNormal();
    const FVector3 RemainingDelta = Delta * RemainingTime;
    const FVector3 SlideDelta = RemainingDelta
        - UnitNormal * FVector3::Dot(RemainingDelta, UnitNormal);
    if (SlideDelta.IsNearlyZero())
    {
        return 0.0f;
    }

    PSceneComponent* Component = GetUpdatedComponent();
    if (Component == nullptr)
    {
        return 0.0f;
    }
    const FVector3 Start = Component->GetWorldTransform().Translation;
    const FHitResult InitialHit = Hit;
    FHitResult SlideHit;
    SafeMoveUpdatedComponent(
        SlideDelta,
        Component->GetWorldTransform().Rotation,
        bSweep,
        &SlideHit);
    Hit = SlideHit.bBlockingHit ? SlideHit : InitialHit;
    LastHitResult = Hit;
    return (Component->GetWorldTransform().Translation - Start).Size();
}

void PMovementComponent::OnRegister()
{
    PActorComponent::OnRegister();
    if (GetUpdatedComponent() == nullptr)
    {
        PActor* Owner = GetOwner();
        SetUpdatedComponent(Owner != nullptr ? Owner->GetRootComponent() : nullptr);
    }
}
}
