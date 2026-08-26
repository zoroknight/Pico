#include "PicoSandbox/SandboxGameplayAbilities.h"

#include "Pico/GameplayAbilities/GameplayTag.h"
#include "Pico/Object/ObjectGlobals.h"

namespace PicoSandbox
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxDashAbility)
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxFireballAbility)
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxStunAbility)

PSandboxDashAbility::PSandboxDashAbility(const Pico::FObjectConstructionParams& Params)
    : PGameplayAbility(Params)
{
}

PSandboxFireballAbility::PSandboxFireballAbility(const Pico::FObjectConstructionParams& Params)
    : PGameplayAbility(Params)
{
}

PSandboxStunAbility::PSandboxStunAbility(const Pico::FObjectConstructionParams& Params)
    : PGameplayAbility(Params)
{
}

bool ConfigureSandboxGameplayAbilityDefaults()
{
    auto& Tags = Pico::FGameplayTagsManager::Get();
    const Pico::FGameplayTag Frozen = Tags.RegisterGameplayTag("State.Frozen");
    Tags.RegisterGameplayTag("State.Burning");
    Tags.RegisterGameplayTag("State.GravityLifted");
    const auto Configure = [Frozen](
        Pico::PGameplayAbility* Ability,
        const char* AbilityTag,
        float Cost,
        float Cooldown)
    {
        return Ability != nullptr
            && Ability->SetDefaultCost(Cost)
            && Ability->SetDefaultCooldown(Cooldown)
            && Ability->AddAbilityTag(
                Pico::FGameplayTagsManager::Get().RegisterGameplayTag(AbilityTag))
            && Ability->AddActivationBlockedTag(Frozen);
    };
    return Configure(Pico::GetMutableDefault<PSandboxDashAbility>(),
               "Ability.Projectile.Gravity", 20.0f, 1.5f)
        && Configure(Pico::GetMutableDefault<PSandboxFireballAbility>(),
               "Ability.Projectile.Burn", 15.0f, 1.0f)
        && Configure(Pico::GetMutableDefault<PSandboxStunAbility>(),
               "Ability.Projectile.Freeze", 25.0f, 3.0f);
}
}
