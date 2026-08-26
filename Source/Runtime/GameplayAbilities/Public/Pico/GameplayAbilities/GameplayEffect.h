#pragma once

#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayTag.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/ReflectionMacros.h"

#include <string>
#include <vector>

namespace Pico
{
enum class EGameplayEffectDurationPolicy : uint8
{
    Instant,
    Duration,
    Infinite
};

enum class EGameplayModifierOperation : uint8
{
    Add,
    Multiply,
    Override
};

struct FGameplayModifierInfo
{
    EGameplayAttribute Attribute = EGameplayAttribute::Health;
    EGameplayModifierOperation Operation = EGameplayModifierOperation::Add;
    float Magnitude = 0.0f;
};

struct FGameplayEffectSpec
{
    const PClass* EffectClass = nullptr;
    FObjectHandle SourceHandle;
    float Level = 1.0f;
    EGameplayEffectDurationPolicy DurationPolicy =
        EGameplayEffectDurationPolicy::Instant;
    float Duration = 0.0f;
    float Period = 0.0f;
    int32 StackLimitCount = 1;
    std::string StackingKey;
    std::vector<FGameplayModifierInfo> Modifiers;
    FGameplayTagContainer ApplicationRequiredTags;
    FGameplayTagContainer ApplicationBlockedTags;
    FGameplayTagContainer GrantedTags;

    bool IsValid() const;
};

struct FActiveGameplayEffectHandle
{
    uint32 Value = 0;

    bool IsValid() const { return Value != 0; }
    void Reset() { Value = 0; }
    friend bool operator==(const FActiveGameplayEffectHandle&, const FActiveGameplayEffectHandle&) = default;
};

struct FAppliedGameplayModifier
{
    EGameplayAttribute Attribute = EGameplayAttribute::Health;
    float AppliedDelta = 0.0f;
};

struct FActiveGameplayEffect
{
    FActiveGameplayEffectHandle Handle;
    FGameplayEffectSpec Spec;
    float RemainingDuration = 0.0f;
    float PeriodAccumulator = 0.0f;
    int32 StackCount = 1;
    std::vector<FAppliedGameplayModifier> PersistentModifiers;
};

class PGameplayEffect : public PObject
{
    PICO_DECLARE_CLASS(PGameplayEffect, PObject)

public:
    EGameplayEffectDurationPolicy GetDurationPolicy() const;
    float GetDuration() const;
    float GetPeriod() const;
    int32 GetStackLimitCount() const;
    const std::vector<FGameplayModifierInfo>& GetModifiers() const;
    const FGameplayTagContainer& GetApplicationRequiredTags() const;
    const FGameplayTagContainer& GetApplicationBlockedTags() const;
    const FGameplayTagContainer& GetGrantedTags() const;

    bool SetDurationPolicy(EGameplayEffectDurationPolicy Policy);
    bool SetDuration(float Value);
    bool SetPeriod(float Value);
    bool SetStackLimitCount(int32 Value);
    bool AddModifier(const FGameplayModifierInfo& Modifier);
    void ClearModifiers();
    bool AddApplicationRequiredTag(const FGameplayTag& Tag);
    bool AddApplicationBlockedTag(const FGameplayTag& Tag);
    bool AddGrantedTag(const FGameplayTag& Tag);

protected:
    explicit PGameplayEffect(const FObjectConstructionParams& Params);

private:
    bool CanEditDefaults() const;

    int32 DurationPolicyValue =
        static_cast<int32>(EGameplayEffectDurationPolicy::Instant);
    float Duration = 0.0f;
    float Period = 0.0f;
    int32 StackLimitCount = 1;
    std::vector<FGameplayModifierInfo> Modifiers;
    FGameplayTagContainer ApplicationRequiredTags;
    FGameplayTagContainer ApplicationBlockedTags;
    FGameplayTagContainer GrantedTags;
};
}
