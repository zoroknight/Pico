#include "Pico/Samples/DemoCharacter.h"

#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PDemoCharacter)

int32 PDemoCharacter::GetHealth() const
{
    return Health;
}

float PDemoCharacter::GetMoveSpeed() const
{
    return MoveSpeed;
}

bool PDemoCharacter::IsAlive() const
{
    return bAlive;
}

int32 PDemoCharacter::GetHealthSeenInPostLoad() const
{
    return HealthSeenInPostLoad;
}

PDemoCharacter::PDemoCharacter(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PDemoCharacter::PostLoad()
{
    HealthSeenInPostLoad = Health;
}

bool PDemoCharacter::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Health);
    PICO_ADD_PROPERTY(Properties, MoveSpeed);
    PICO_ADD_PROPERTY(Properties, bAlive);
    return Class.AddProperties(std::move(Properties));
}
}
