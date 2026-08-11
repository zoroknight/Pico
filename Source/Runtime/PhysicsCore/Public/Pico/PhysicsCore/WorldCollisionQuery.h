#pragma once

#include "Pico/PhysicsCore/CollisionTypes.h"

namespace Pico
{
class IWorldCollisionQuery
{
public:
    virtual ~IWorldCollisionQuery() = default;

    virtual bool Sweep(
        const FCollisionShape& Shape,
        const FVector3& Start,
        const FVector3& End,
        const FQuat& Rotation,
        const FCollisionQueryParams& Params,
        FHitResult& OutHit) const = 0;

    virtual bool Raycast(
        const FVector3& Start,
        const FVector3& End,
        const FCollisionQueryParams& Params,
        FHitResult& OutHit) const;

    virtual bool Overlap(
        const FCollisionShape& Shape,
        const FVector3& Location,
        const FQuat& Rotation,
        const FCollisionQueryParams& Params,
        std::vector<FOverlapResult>& OutOverlaps) const;
};

class FNullWorldCollisionQuery final : public IWorldCollisionQuery
{
public:
    bool Sweep(
        const FCollisionShape& Shape,
        const FVector3& Start,
        const FVector3& End,
        const FQuat& Rotation,
        const FCollisionQueryParams& Params,
        FHitResult& OutHit) const override;
};
}
