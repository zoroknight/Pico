#include "PicoSandbox/SandboxCharacter.h"

#include <utility>
#include <vector>

namespace PicoSandbox
{
PICO_DEFINE_CLASS(PSandboxCharacter)

bool PSandboxCharacter::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Health);
    PICO_ADD_PROPERTY(Properties, MoveSpeed);
    PICO_ADD_PROPERTY(Properties, bAlive);
    //
    PICO_ADD_PROPERTY(Properties, Mana);
    PICO_ADD_PROPERTY(Properties, Velocity);
    PICO_ADD_PROPERTY(Properties, ViewRotation);
    PICO_ADD_PROPERTY(Properties, Transform);
    return Class.AddProperties(std::move(Properties));
}

PSandboxCharacter::PSandboxCharacter(const Pico::FObjectConstructionParams& Params)
    : PSandboxEntity(Params)
{
}

Pico::int32 PSandboxCharacter::GetHealth() const
{
    return Health;
}

float PSandboxCharacter::GetMoveSpeed() const
{
    return MoveSpeed;
}

bool PSandboxCharacter::IsAlive() const
{
    return bAlive;
}

const Pico::FVector3& PSandboxCharacter::GetVelocity() const
{
    return Velocity;
}

const Pico::FRotator& PSandboxCharacter::GetViewRotation() const
{
    return ViewRotation;
}

const Pico::FTransform& PSandboxCharacter::GetTransform() const
{
    return Transform;
}

Pico::int32 PSandboxCharacter::GetHealthSeenInPostLoad() const
{
    return HealthSeenInPostLoad;
}

const Pico::FTransform& PSandboxCharacter::GetTransformSeenInPostLoad() const
{
    return TransformSeenInPostLoad;
}

void PSandboxCharacter::PostLoad()
{
    HealthSeenInPostLoad = Health;
    TransformSeenInPostLoad = Transform;
}
}
