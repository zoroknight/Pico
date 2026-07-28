#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/ActorComponent.h"

namespace Pico
{
class PSceneComponent : public PActorComponent
{
    PICO_DECLARE_CLASS(PSceneComponent, PActorComponent)

public:
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

private:
    FTransform RelativeTransform;
};
}
