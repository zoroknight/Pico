#include "PicoSandbox/SandboxEntity.h"

#include <utility>
#include <vector>

namespace PicoSandbox
{
PICO_DEFINE_CLASS(PSandboxEntity)

bool PSandboxEntity::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, EntityId);
    PICO_ADD_PROPERTY(Properties, bEnabled);
    return Class.AddProperties(std::move(Properties));
}

PSandboxEntity::PSandboxEntity(const Pico::FObjectConstructionParams& Params)
    : PObject(Params)
{
}

Pico::int32 PSandboxEntity::GetEntityId() const
{
    return EntityId;
}

bool PSandboxEntity::IsEnabled() const
{
    return bEnabled;
}
}
