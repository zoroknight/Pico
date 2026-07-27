#pragma once

#include "PicoSandbox/SandboxEntity.h"

namespace PicoSandbox
{
class PSandboxCharacter final : public PSandboxEntity
{
    PICO_DECLARE_CLASS(PSandboxCharacter, PSandboxEntity)

public:
    Pico::int32 GetHealth() const;
    float GetMoveSpeed() const;
    bool IsAlive() const;
    const Pico::FVector3& GetVelocity() const;
    const Pico::FRotator& GetViewRotation() const;
    const Pico::FTransform& GetTransform() const;
    Pico::int32 GetHealthSeenInPostLoad() const;
    const Pico::FTransform& GetTransformSeenInPostLoad() const;

protected:
    explicit PSandboxCharacter(const Pico::FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    Pico::int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
    Pico::int32 HealthSeenInPostLoad = 0;

    //
    Pico::int32 Mana = 50;
    Pico::FVector3 Velocity;
    Pico::FRotator ViewRotation;
    Pico::FTransform Transform;
    Pico::FTransform TransformSeenInPostLoad;
};
}
