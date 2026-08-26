#include "Pico/GameplayAbilities/GameplayEffect.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
bool FGameplayEffectSpec::IsValid() const
{
    const bool bValidDurationPolicy =
        DurationPolicy == EGameplayEffectDurationPolicy::Instant
        || DurationPolicy == EGameplayEffectDurationPolicy::Duration
        || DurationPolicy == EGameplayEffectDurationPolicy::Infinite;
    const bool bValidModifiers = std::all_of(
        Modifiers.begin(), Modifiers.end(),
        [](const FGameplayModifierInfo& Modifier)
        {
            const bool bValidOperation =
                Modifier.Operation == EGameplayModifierOperation::Add
                || Modifier.Operation == EGameplayModifierOperation::Multiply
                || Modifier.Operation == EGameplayModifierOperation::Override;
            return bValidOperation && std::isfinite(Modifier.Magnitude);
        });
    return EffectClass != nullptr
        && EffectClass->IsChildOf(PGameplayEffect::StaticClass())
        && bValidDurationPolicy
        && std::isfinite(Level)
        && Level > 0.0f
        && std::isfinite(Duration)
        && Duration >= 0.0f
        && std::isfinite(Period)
        && Period >= 0.0f
        && StackLimitCount >= 1
        && (DurationPolicy != EGameplayEffectDurationPolicy::Duration
            || Duration > 0.0f)
        && bValidModifiers;
}

PICO_DEFINE_CLASS(PGameplayEffect)

bool PGameplayEffect::RegisterProperties(PClass& Class)
{
    FPropertyMetadata PolicyMetadata;
    PolicyMetadata.Flags = EPropertyFlags::Editable | EPropertyFlags::Serializable;
    PolicyMetadata.EnumOptions = {
        {static_cast<int32>(EGameplayEffectDurationPolicy::Instant), "Instant"},
        {static_cast<int32>(EGameplayEffectDurationPolicy::Duration), "Duration"},
        {static_cast<int32>(EGameplayEffectDurationPolicy::Infinite), "Infinite"}};
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Editable | EPropertyFlags::Serializable;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, DurationPolicyValue, PolicyMetadata);
    PICO_ADD_PROPERTY_METADATA(Properties, Duration, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, Period, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, StackLimitCount, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PGameplayEffect::PGameplayEffect(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

EGameplayEffectDurationPolicy PGameplayEffect::GetDurationPolicy() const
{
    return static_cast<EGameplayEffectDurationPolicy>(DurationPolicyValue);
}

float PGameplayEffect::GetDuration() const { return Duration; }
float PGameplayEffect::GetPeriod() const { return Period; }
int32 PGameplayEffect::GetStackLimitCount() const { return StackLimitCount; }
const std::vector<FGameplayModifierInfo>& PGameplayEffect::GetModifiers() const { return Modifiers; }
const FGameplayTagContainer& PGameplayEffect::GetApplicationRequiredTags() const { return ApplicationRequiredTags; }
const FGameplayTagContainer& PGameplayEffect::GetApplicationBlockedTags() const { return ApplicationBlockedTags; }
const FGameplayTagContainer& PGameplayEffect::GetGrantedTags() const { return GrantedTags; }

bool PGameplayEffect::SetDurationPolicy(EGameplayEffectDurationPolicy Policy)
{
    if (!CanEditDefaults()) return false;
    DurationPolicyValue = static_cast<int32>(Policy);
    return true;
}

bool PGameplayEffect::SetDuration(float Value)
{
    if (!CanEditDefaults() || !std::isfinite(Value)) return false;
    Duration = std::max(0.0f, Value);
    return true;
}

bool PGameplayEffect::SetPeriod(float Value)
{
    if (!CanEditDefaults() || !std::isfinite(Value)) return false;
    Period = std::max(0.0f, Value);
    return true;
}

bool PGameplayEffect::SetStackLimitCount(int32 Value)
{
    if (!CanEditDefaults() || Value < 1) return false;
    StackLimitCount = Value;
    return true;
}

bool PGameplayEffect::AddModifier(const FGameplayModifierInfo& Modifier)
{
    if (!CanEditDefaults() || !std::isfinite(Modifier.Magnitude)) return false;
    Modifiers.push_back(Modifier);
    return true;
}

void PGameplayEffect::ClearModifiers()
{
    if (CanEditDefaults()) Modifiers.clear();
}

bool PGameplayEffect::AddApplicationRequiredTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && ApplicationRequiredTags.AddTag(Tag);
}

bool PGameplayEffect::AddApplicationBlockedTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && ApplicationBlockedTags.AddTag(Tag);
}

bool PGameplayEffect::AddGrantedTag(const FGameplayTag& Tag)
{
    return CanEditDefaults() && GrantedTags.AddTag(Tag);
}

bool PGameplayEffect::CanEditDefaults() const
{
    return CheckGameThread("PGameplayEffect::EditDefaults")
        && HasAnyFlags(GetFlags(), EObjectFlags::ClassDefaultObject);
}
}
