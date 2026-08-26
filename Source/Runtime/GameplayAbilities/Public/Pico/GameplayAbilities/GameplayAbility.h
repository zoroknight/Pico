#pragma once

#include "Pico/GameplayAbilities/GameplayTag.h"
#include "Pico/Object/ReflectionMacros.h"

#include <cstdint>

namespace Pico
{
class PGameplayAbilitySystemComponent;

struct FGameplayAbilitySpecHandle
{
    uint32 Value = 0;

    bool IsValid() const { return Value != 0; }
    void Reset() { Value = 0; }
    friend bool operator==(const FGameplayAbilitySpecHandle&, const FGameplayAbilitySpecHandle&) = default;
};

enum class EGameplayAbilityActivationState : uint8
{
    Inactive,
    Active
};

struct FGameplayAbilitySpec
{
    FGameplayAbilitySpecHandle Handle;
    const PClass* AbilityClass = nullptr;
    int32 Level = 1;
    int32 InputId = -1;
    float CostOverride = -1.0f;
    float CooldownOverride = -1.0f;
    EGameplayAbilityActivationState ActivationState =
        EGameplayAbilityActivationState::Inactive;

    bool IsActive() const
    {
        return ActivationState == EGameplayAbilityActivationState::Active;
    }

    float ResolveCost(float ClassDefault) const
    {
        return CostOverride >= 0.0f ? CostOverride : ClassDefault;
    }

    float ResolveCooldown(float ClassDefault) const
    {
        return CooldownOverride >= 0.0f ? CooldownOverride : ClassDefault;
    }
};

class PGameplayAbility : public PObject
{
    PICO_DECLARE_CLASS(PGameplayAbility, PObject)

public:
    float GetDefaultCost() const;
    float GetDefaultCooldown() const;
    bool IsCancelable() const;
    const FGameplayTagContainer& GetAbilityTags() const;
    const FGameplayTagContainer& GetActivationRequiredTags() const;
    const FGameplayTagContainer& GetActivationBlockedTags() const;
    const FGameplayTagContainer& GetActivationOwnedTags() const;

    bool SetDefaultCost(float Value);
    bool SetDefaultCooldown(float Value);
    bool SetCancelable(bool bValue);
    bool AddAbilityTag(const FGameplayTag& Tag);
    bool AddActivationRequiredTag(const FGameplayTag& Tag);
    bool AddActivationBlockedTag(const FGameplayTag& Tag);
    bool AddActivationOwnedTag(const FGameplayTag& Tag);

protected:
    explicit PGameplayAbility(const FObjectConstructionParams& Params);

    virtual bool CanActivateAbility(
        const PGameplayAbilitySystemComponent& AbilitySystem,
        const FGameplayAbilitySpec& Spec) const;
    virtual bool ActivateAbility(
        PGameplayAbilitySystemComponent& AbilitySystem,
        FGameplayAbilitySpecHandle Handle) const;
    virtual bool CommitAbility(
        PGameplayAbilitySystemComponent& AbilitySystem,
        FGameplayAbilitySpecHandle Handle) const;
    virtual void CancelAbility(
        PGameplayAbilitySystemComponent& AbilitySystem,
        FGameplayAbilitySpecHandle Handle) const;
    virtual void EndAbility(
        PGameplayAbilitySystemComponent& AbilitySystem,
        FGameplayAbilitySpecHandle Handle,
        bool bWasCancelled) const;

private:
    friend class PGameplayAbilitySystemComponent;

    bool CanEditDefaults() const;

    float DefaultCost = 0.0f;
    float DefaultCooldown = 0.0f;
    bool bCancelable = true;
    FGameplayTagContainer AbilityTags;
    FGameplayTagContainer ActivationRequiredTags;
    FGameplayTagContainer ActivationBlockedTags;
    FGameplayTagContainer ActivationOwnedTags;
};
}
