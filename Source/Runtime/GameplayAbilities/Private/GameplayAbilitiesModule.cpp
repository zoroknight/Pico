#include "Pico/GameplayAbilities/GameplayAbilitiesModule.h"

#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/AbilityTask.h"
#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"

namespace Pico
{
bool RegisterGameplayAbilitiesClasses()
{
    return PAttributeSet::RegisterClass()
        && PGameplayEffect::RegisterClass()
        && PGameplayAbility::RegisterClass()
        && PAbilityTask::RegisterClass()
        && PAbilityTaskWaitDelay::RegisterClass()
        && PAbilityTaskWaitGameplayEvent::RegisterClass()
        && PAbilityTaskPlayAnimationAndWait::RegisterClass()
        && PGameplayAbilitySystemComponent::RegisterClass();
}
}
