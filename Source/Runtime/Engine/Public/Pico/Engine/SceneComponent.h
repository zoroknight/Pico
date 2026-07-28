#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/ActorComponent.h"

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
    std::vector<PSceneComponent*> GetAttachChildren() const;
    bool IsAttachedTo(const PSceneComponent* Component) const;
    bool AttachToComponent(PSceneComponent* Parent, EAttachmentTransformRule Rule);
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
    void SetWorldTransform(const FTransform& Transform);

protected:
    explicit PSceneComponent(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

private:
    PSceneComponent* ResolveSceneComponent(FObjectHandle Handle) const;
    void AddAttachChild(FObjectHandle Handle);
    void RemoveAttachChild(FObjectHandle Handle);

    FObjectHandle AttachParentHandle;
    std::vector<FObjectHandle> AttachChildrenHandles;
    FTransform RelativeTransform;
};
}
