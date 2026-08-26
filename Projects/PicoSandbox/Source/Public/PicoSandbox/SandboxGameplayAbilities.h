#pragma once

#include "Pico/GameplayAbilities/GameplayAbility.h"

namespace PicoSandbox
{
class PSandboxDashAbility final : public Pico::PGameplayAbility
{
    PICO_DECLARE_CLASS(PSandboxDashAbility, Pico::PGameplayAbility)
protected:
    explicit PSandboxDashAbility(const Pico::FObjectConstructionParams& Params);
};

class PSandboxFireballAbility final : public Pico::PGameplayAbility
{
    PICO_DECLARE_CLASS(PSandboxFireballAbility, Pico::PGameplayAbility)
protected:
    explicit PSandboxFireballAbility(const Pico::FObjectConstructionParams& Params);
};

class PSandboxStunAbility final : public Pico::PGameplayAbility
{
    PICO_DECLARE_CLASS(PSandboxStunAbility, Pico::PGameplayAbility)
protected:
    explicit PSandboxStunAbility(const Pico::FObjectConstructionParams& Params);
};

bool ConfigureSandboxGameplayAbilityDefaults();
}
