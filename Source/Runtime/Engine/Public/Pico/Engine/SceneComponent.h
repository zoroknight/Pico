#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/PhysicsCore/CollisionTypes.h"

#include <vector>

namespace Pico
{
enum class EAttachmentTransformRule
{
    KeepRelative,
    KeepWorld
};

class PSceneComponent : public PActorComponent
{
    PICO_DECLARE_CLASS(PSceneComponent, PActorComponent)

public:
    PSceneComponent* GetAttachParent() const;
    FName GetAttachSocketName() const;
    std::vector<PSceneComponent*> GetAttachChildren() const;
    bool IsAttachedTo(const PSceneComponent* Component) const;
    bool AttachToComponent(
        PSceneComponent* Parent,
        EAttachmentTransformRule Rule,
        FName SocketName = {});
    bool DetachFromComponent(EAttachmentTransformRule Rule);

    const FTransform& GetRelativeTransform() const;
    void SetRelativeTransform(const FTransform& Transform);

    const FVector3& GetRelativeLocation() const;
    void SetRelativeLocation(const FVector3& Location);

    FRotator GetRelativeRotation() const;
    void SetRelativeRotation(const FRotator& Rotation);

    const FVector3& GetRelativeScale() const;
    void SetRelativeScale(const FVector3& Scale);

    FTransform GetWorldTransform() const;
    virtual bool DoesSocketExist(FName SocketName) const;
    virtual FTransform GetSocketTransform(FName SocketName) const;
    void SetWorldTransform(const FTransform& Transform);
    virtual FCollisionShape GetCollisionShape() const;
    virtual bool MoveComponent(
        const FVector3& Delta,
        const FQuat& NewRotation,
        bool bSweep,
        FHitResult* OutHit = nullptr,
        EMoveComponentFlags MoveFlags = EMoveComponentFlags::None,
        ETeleportType Teleport = ETeleportType::None);

protected:
    explicit PSceneComponent(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    virtual void OnWorldTransformChanged(ETeleportType Teleport);
    void SetWorldTransformFromPhysics(const FTransform& Transform);

private:
    void SetWorldTransformInternal(const FTransform& Transform, ETeleportType Teleport);
    void NotifyTransformChangedRecursive(ETeleportType Teleport);
    PSceneComponent* ResolveSceneComponent(FObjectHandle Handle) const;
    void AddAttachChild(FObjectHandle Handle);
    void RemoveAttachChild(FObjectHandle Handle);

    FObjectHandle AttachParentHandle;
    FName AttachSocketName;
    std::vector<FObjectHandle> AttachChildrenHandles;
    FTransform RelativeTransform;
    bool bApplyingPhysicsTransform = false;
};
}
