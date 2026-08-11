#include "Pico/PhysicsCore/WorldCollisionQuery.h"

namespace Pico
{
bool IWorldCollisionQuery::Raycast(
    const FVector3& Start,
    const FVector3& End,
    const FCollisionQueryParams&,
    FHitResult& OutHit) const
{
    OutHit.Reset(Start, End);
    return false;
}

bool IWorldCollisionQuery::Overlap(
    const FCollisionShape&,
    const FVector3&,
    const FQuat&,
    const FCollisionQueryParams&,
    std::vector<FOverlapResult>& OutOverlaps) const
{
    OutOverlaps.clear();
    return false;
}

bool FNullWorldCollisionQuery::Sweep(
    const FCollisionShape&,
    const FVector3& Start,
    const FVector3& End,
    const FQuat&,
    const FCollisionQueryParams&,
    FHitResult& OutHit) const
{
    OutHit.Reset(Start, End);
    return false;
}
}
