#pragma once

#include "Pico/Engine/ActorComponent.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/PhysicsCore/CollisionTypes.h"

namespace Pico
{
class PSceneComponent;

class PMovementComponent : public PActorComponent
{
    PICO_DECLARE_CLASS(PMovementComponent, PActorComponent)

public:
    PSceneComponent* GetUpdatedComponent() const;
    virtual bool SetUpdatedComponent(PSceneComponent* Component);

    const FVector3& GetVelocity() const;
    void SetVelocity(const FVector3& InVelocity);
    const FVector3& GetLastMoveDelta() const;
    const FHitResult& GetLastHitResult() const;
    bool WasLastMoveSwept() const;
    ETeleportType GetLastTeleportType() const;

    bool MoveUpdatedComponent(
        const FVector3& Delta,
        const FQuat& NewRotation,
        bool bSweep,
        FHitResult* OutHit = nullptr,
        ETeleportType Teleport = ETeleportType::None);
    bool SafeMoveUpdatedComponent(
        const FVector3& Delta,
        const FQuat& NewRotation,
        bool bSweep,
        FHitResult* OutHit = nullptr,
        ETeleportType Teleport = ETeleportType::None);
    float SlideAlongSurface(
        const FVector3& Delta,
        float Time,
        const FVector3& Normal,
        FHitResult& Hit,
        bool bSweep = true);

protected:
    explicit PMovementComponent(const FObjectConstructionParams& Params);
    void OnRegister() override;

private:
    TWeakObjectPtr<PSceneComponent> UpdatedComponent;
    FVector3 Velocity = FVector3::ZeroVector;
    FVector3 LastMoveDelta = FVector3::ZeroVector;
    FHitResult LastHitResult;
    ETeleportType LastTeleportType = ETeleportType::None;
    bool bLastMoveSwept = false;
};
}
