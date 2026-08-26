#include "Pico/GameplayAbilities/GameplayAbility.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/Actor.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
FGameplayTag GetCooldownTag(const FGameplayAbilitySpec& Spec)
{
    return Spec.AbilityClass != nullptr
        ? FGameplayTagsManager::Get().RegisterGameplayTag(
            "Cooldown." + Spec.AbilityClass->GetName().ToString())
        : FGameplayTag {};
}
}

PICO_DEFINE_CLASS(PGameplayAbility)

bool PGameplayAbility::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Editable | EPropertyFlags::Serializable;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, DefaultCost, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, DefaultCooldown, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, bCancelable, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PGameplayAbility::PGameplayAbility(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

float PGameplayAbility::GetDefaultCost() const { return DefaultCost; }
float PGameplayAbility::GetDefaultCooldown() const { return DefaultCooldown; }
bool PGameplayAbility::IsCancelable() const { return bCancelable; }
const FGameplayTagContainer& PGameplayAbility::GetAbilityTags() const { return AbilityTags; }
const FGameplayTagContainer& PGameplayAbility::GetActivationRequiredTags() const { return ActivationRequiredTags; }
const FGameplayTagContainer& PGameplayAbility::GetActivationBlockedTags() const { return ActivationBlockedTags; }
const FGameplayTagContainer& PGameplayAbility::GetActivationOwnedTags() const { return ActivationOwnedTags; }

bool PGameplayAbility::SetDefaultCost(float Value)
{
    if (!CanEditDefaults() || !std::isfinite(Value)) return false;
    DefaultCost = std::max(0.0f, Value);
    return true;
}

bool PGameplayAbility::SetDefaultCooldown(float Value)
{
    if (!CanEditDefaults() || !std::isfinite(Value)) return false;
    DefaultCooldown = std::max(0.0f, Value);
    return true;
}

bool PGameplayAbility::SetCancelable(bool bValue)
{
    if (!CanEditDefaults()) return false;
    bCancelable = bValue;
    return true;
}

bool PGameplayAbility::AddAbilityTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && AbilityTags.AddTag(Tag);
}

bool PGameplayAbility::AddActivationRequiredTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && ActivationRequiredTags.AddTag(Tag);
}

bool PGameplayAbility::AddActivationBlockedTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && ActivationBlockedTags.AddTag(Tag);
}

bool PGameplayAbility::AddActivationOwnedTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && ActivationOwnedTags.AddTag(Tag);
}

bool PGameplayAbility::CanActivateAbility(
    const PGameplayAbilitySystemComponent& AbilitySystem,
    const FGameplayAbilitySpec& Spec) const
{
    const PAttributeSet* Attributes = AbilitySystem.GetAttributeSet();
    const FGameplayTag CooldownTag = GetCooldownTag(Spec);
    const float Cost = Spec.ResolveCost(DefaultCost);
    const float Cooldown = Spec.ResolveCooldown(DefaultCooldown);
    const float ScaledCost = Cost * static_cast<float>(Spec.Level);
    return AbilitySystem.HasAllMatchingGameplayTags(ActivationRequiredTags)
        && !AbilitySystem.HasAnyMatchingGameplayTags(ActivationBlockedTags)
        && (ScaledCost <= 0.0f
            || (Attributes != nullptr
                && Attributes->GetCurrentValue(EGameplayAttribute::Mana) >= ScaledCost))
        && (Cooldown <= 0.0f
            || !AbilitySystem.HasMatchingGameplayTag(CooldownTag));
}

bool PGameplayAbility::ActivateAbility(
    PGameplayAbilitySystemComponent&,
    FGameplayAbilitySpecHandle) const
{
    return true;
}

bool PGameplayAbility::CommitAbility(
    PGameplayAbilitySystemComponent& AbilitySystem,
    FGameplayAbilitySpecHandle Handle) const
{
    const FGameplayAbilitySpec* AbilitySpec = AbilitySystem.FindAbilitySpec(Handle);
    if (AbilitySpec == nullptr) return false;
    const float Cost = AbilitySpec->ResolveCost(DefaultCost);
    const float Cooldown = AbilitySpec->ResolveCooldown(DefaultCooldown);

    if (Cost > 0.0f)
    {
        FGameplayEffectSpec CostSpec = AbilitySystem.MakeOutgoingSpec(
            PGameplayEffect::StaticClass(),
            static_cast<float>(AbilitySpec->Level),
            AbilitySystem.GetAbilityOwnerActor());
        CostSpec.StackingKey = "Cost." + AbilitySpec->AbilityClass->GetName().ToString();
        CostSpec.Modifiers = {{
            EGameplayAttribute::Mana,
            EGameplayModifierOperation::Add,
            -Cost}};
        if (!AbilitySystem.ApplyGameplayEffectSpecToSelf(CostSpec)) return false;
    }

    if (Cooldown > 0.0f)
    {
        FGameplayEffectSpec CooldownSpec = AbilitySystem.MakeOutgoingSpec(
            PGameplayEffect::StaticClass(),
            1.0f,
            AbilitySystem.GetAbilityOwnerActor());
        CooldownSpec.DurationPolicy = EGameplayEffectDurationPolicy::Duration;
        CooldownSpec.Duration = Cooldown;
        CooldownSpec.StackingKey =
            "Cooldown." + AbilitySpec->AbilityClass->GetName().ToString();
        CooldownSpec.GrantedTags.AddTag(GetCooldownTag(*AbilitySpec));
        if (!AbilitySystem.ApplyGameplayEffectSpecToSelf(CooldownSpec)) return false;
    }
    return true;
}

void PGameplayAbility::CancelAbility(
    PGameplayAbilitySystemComponent&,
    FGameplayAbilitySpecHandle) const
{
}

void PGameplayAbility::EndAbility(
    PGameplayAbilitySystemComponent&,
    FGameplayAbilitySpecHandle,
    bool) const
{
}

bool PGameplayAbility::CanEditDefaults() const
{
    return CheckGameThread("PGameplayAbility::EditDefaults")
        && HasAnyFlags(GetFlags(), EObjectFlags::ClassDefaultObject);
}
}
