#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/PhysicsCore/PhysicsScene.h"

namespace Pico
{
class PPrimitiveComponent;
using FComponentPhysicsEvent =
    TObjectMulticastDelegate<void(PPrimitiveComponent*, const FPhysicsContactEvent&)>;

class PPrimitiveComponent : public PSceneComponent
{
    PICO_DECLARE_CLASS(PPrimitiveComponent, PSceneComponent)

public:
    bool IsVisible() const;
    void SetVisible(bool bInVisible);

    const FVector3& GetColor() const;
    void SetColor(const FVector3& InColor);

    ECollisionEnabled GetCollisionEnabled() const;
    void SetCollisionEnabled(ECollisionEnabled InCollisionEnabled);
    ECollisionProfile GetCollisionProfile() const;
    void SetCollisionProfile(ECollisionProfile Profile);
    FCollisionFilterData GetCollisionFilterData() const;
    EPhysicsBodyType GetPhysicsBodyType() const;
    void SetPhysicsBodyType(EPhysicsBodyType InBodyType);
    bool IsSimulatingPhysics() const;
    void SetSimulatePhysics(bool bValue);
    bool IsSensor() const;
    void SetSensor(bool bValue);
    bool IsGravityEnabled() const;
    void SetGravityEnabled(bool bValue);
    float GetMass() const;
    void SetMass(float InMass);
    FPhysicsBodyHandle GetPhysicsBodyHandle() const;
    bool GetPhysicsBodyState(FPhysicsBodyState& OutState) const;
    bool ApplyReplicatedPhysicsBodyState(const FPhysicsBodyState& State);
    bool IsNetworkPhysicsProxy() const;
    void SetNetworkPhysicsProxy(bool bInNetworkPhysicsProxy);
    bool IsPhysicsContactEnabled() const;
    void SetPhysicsContactEnabled(bool bEnabled);
    bool AddImpulse(const FVector3& Impulse);

    FComponentPhysicsEvent& OnComponentHit();
    FComponentPhysicsEvent& OnComponentBeginOverlap();
    FComponentPhysicsEvent& OnComponentEndOverlap();

    void SyncComponentFromPhysics();
    void DispatchPhysicsEvent(
        PPrimitiveComponent* Other,
        const FPhysicsContactEvent& Event);
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;

protected:
    explicit PPrimitiveComponent(const FObjectConstructionParams& Params);
    void PostLoad() override;
    void OnRegister() override;
    void OnUnregister() override;
    void OnWorldTransformChanged(ETeleportType Teleport) override;
    void RecreatePhysicsState();

private:
    void ApplyCollisionProfile();
    void CreatePhysicsState();
    void DestroyPhysicsState();

    bool bVisible = true;
    FVector3 Color = FVector3(0.16f, 0.62f, 0.52f);
    int32 CollisionProfileValue = static_cast<int32>(ECollisionProfile::Custom);
    int32 CollisionEnabledValue = static_cast<int32>(ECollisionEnabled::NoCollision);
    int32 PhysicsBodyTypeValue = static_cast<int32>(EPhysicsBodyType::Static);
    bool bSimulatePhysics = false;
    bool bSensor = false;
    bool bUseGravity = true;
    float Mass = 1.0f;
    FPhysicsBodyHandle PhysicsBodyHandle;
    bool bNetworkPhysicsProxy = false;
    bool bPhysicsContactEnabled = true;
    FVector3 PhysicsShapeScale = FVector3::OneVector;
    FComponentPhysicsEvent ComponentHitEvent;
    FComponentPhysicsEvent ComponentBeginOverlapEvent;
    FComponentPhysicsEvent ComponentEndOverlapEvent;
};
}
