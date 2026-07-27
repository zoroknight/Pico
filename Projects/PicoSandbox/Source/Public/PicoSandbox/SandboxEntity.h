#pragma once

#include "Pico/Object/Object.h"
#include "Pico/Object/ReflectionMacros.h"

namespace PicoSandbox
{
class PSandboxEntity : public Pico::PObject
{
    PICO_DECLARE_CLASS(PSandboxEntity, Pico::PObject)

public:
    Pico::int32 GetEntityId() const;
    bool IsEnabled() const;

protected:
    explicit PSandboxEntity(const Pico::FObjectConstructionParams& Params);

private:
    Pico::int32 EntityId = 1001;
    bool bEnabled = true;
};
}
