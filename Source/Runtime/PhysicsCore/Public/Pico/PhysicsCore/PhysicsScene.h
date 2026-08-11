#pragma once

#include "Pico/PhysicsCore/WorldCollisionQuery.h"

#include <vector>

namespace Pico
{
class IPhysicsScene : public IWorldCollisionQuery
{
public:
    ~IPhysicsScene() override = default;

    virtual bool IsValid() const = 0;
    virtual uint32 Step(float DeltaSeconds) = 0;
    virtual FPhysicsBodyHandle CreateBody(const FPhysicsBodyDesc& Desc) = 0;
    virtual void DestroyBody(FPhysicsBodyHandle Handle) = 0;
    virtual bool SetBodyTransform(
        FPhysicsBodyHandle Handle,
        const FTransform& Transform,
        ETeleportType Teleport) = 0;
    virtual bool GetBodyState(
        FPhysicsBodyHandle Handle,
        FPhysicsBodyState& OutState) const = 0;
    virtual bool AddImpulse(
        FPhysicsBodyHandle Handle,
        const FVector3& Impulse) = 0;
    virtual std::vector<FPhysicsContactEvent> DrainContactEvents() = 0;
};
}
