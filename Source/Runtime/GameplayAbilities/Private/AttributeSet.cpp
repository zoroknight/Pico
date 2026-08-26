#include "Pico/GameplayAbilities/AttributeSet.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <vector>

namespace Pico
{
std::string_view ToString(EGameplayAttribute Attribute)
{
    switch (Attribute)
    {
    case EGameplayAttribute::Health: return "Health";
    case EGameplayAttribute::MaxHealth: return "MaxHealth";
    case EGameplayAttribute::Mana: return "Mana";
    case EGameplayAttribute::MoveSpeed: return "MoveSpeed";
    }
    return "Unknown";
}

bool TryParseGameplayAttribute(std::string_view Name, EGameplayAttribute& OutAttribute)
{
    for (const EGameplayAttribute Attribute : {
        EGameplayAttribute::Health,
        EGameplayAttribute::MaxHealth,
        EGameplayAttribute::Mana,
        EGameplayAttribute::MoveSpeed})
    {
        if (Name == ToString(Attribute))
        {
            OutAttribute = Attribute;
            return true;
        }
    }
    return false;
}

PICO_DEFINE_CLASS(PAttributeSet)

bool PAttributeSet::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Serializable | EPropertyFlags::ReadOnly;
    Metadata.Description = "Gameplay attribute values must be changed through PAttributeSet";
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, BaseHealth, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, Health, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, BaseMaxHealth, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, MaxHealth, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, BaseMana, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, Mana, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, BaseMoveSpeed, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, MoveSpeed, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PAttributeSet::PAttributeSet(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

FGameplayAttributeData PAttributeSet::GetAttributeData(EGameplayAttribute Attribute) const
{
    const FConstAttributeAddresses Addresses = FindAttribute(Attribute);
    return Addresses.Base != nullptr && Addresses.Current != nullptr
        ? FGameplayAttributeData {*Addresses.Base, *Addresses.Current}
        : FGameplayAttributeData {};
}

float PAttributeSet::GetBaseValue(EGameplayAttribute Attribute) const
{
    return GetAttributeData(Attribute).BaseValue;
}

float PAttributeSet::GetCurrentValue(EGameplayAttribute Attribute) const
{
    return GetAttributeData(Attribute).CurrentValue;
}

bool PAttributeSet::SetBaseValue(EGameplayAttribute Attribute, float Value, FName Source)
{
    if (!CheckGameThread("PAttributeSet::SetBaseValue") || !std::isfinite(Value))
    {
        return false;
    }
    FAttributeAddresses Addresses = FindAttribute(Attribute);
    if (Addresses.Base == nullptr || Addresses.Current == nullptr)
    {
        return false;
    }

    const FGameplayAttributeData OldValue {*Addresses.Base, *Addresses.Current};
    const FGameplayAttributeData OldHealth =
        GetAttributeData(EGameplayAttribute::Health);
    const float NewBase = ClampValue(Attribute, Value);
    const float Delta = NewBase - *Addresses.Base;
    *Addresses.Base = NewBase;
    *Addresses.Current = ClampValue(Attribute, *Addresses.Current + Delta);
    BroadcastChange(Attribute, OldValue, Source);

    if (Attribute == EGameplayAttribute::MaxHealth)
    {
        BaseHealth = std::clamp(BaseHealth, 0.0f, BaseMaxHealth);
        Health = std::clamp(Health, 0.0f, MaxHealth);
        BroadcastChange(EGameplayAttribute::Health, OldHealth, Source);
    }
    return true;
}

bool PAttributeSet::SetCurrentValue(EGameplayAttribute Attribute, float Value, FName Source)
{
    if (!CheckGameThread("PAttributeSet::SetCurrentValue") || !std::isfinite(Value))
    {
        return false;
    }
    FAttributeAddresses Addresses = FindAttribute(Attribute);
    if (Addresses.Base == nullptr || Addresses.Current == nullptr)
    {
        return false;
    }
    const FGameplayAttributeData OldValue {*Addresses.Base, *Addresses.Current};
    const FGameplayAttributeData OldHealth =
        GetAttributeData(EGameplayAttribute::Health);
    *Addresses.Current = ClampValue(Attribute, Value);
    BroadcastChange(Attribute, OldValue, Source);

    if (Attribute == EGameplayAttribute::MaxHealth)
    {
        Health = std::clamp(Health, 0.0f, MaxHealth);
        BroadcastChange(EGameplayAttribute::Health, OldHealth, Source);
    }
    return true;
}

bool PAttributeSet::ModifyCurrentValue(
    EGameplayAttribute Attribute,
    float Delta,
    FName Source)
{
    return std::isfinite(Delta)
        && SetCurrentValue(Attribute, GetCurrentValue(Attribute) + Delta, Source);
}

FOnGameplayAttributeChanged& PAttributeSet::OnAttributeChanged()
{
    return AttributeChangedEvent;
}

const FOnGameplayAttributeChanged& PAttributeSet::OnAttributeChanged() const
{
    return AttributeChangedEvent;
}

std::string PAttributeSet::GetDebugDescription() const
{
    std::ostringstream Stream;
    Stream << "Health=" << Health << '/' << MaxHealth
        << " Mana=" << Mana
        << " MoveSpeed=" << MoveSpeed;
    return Stream.str();
}

void PAttributeSet::PostLoad()
{
    PObject::PostLoad();
    SanitizeValues();
}

void PAttributeSet::BeginDestroy()
{
    AttributeChangedEvent.Clear();
    PObject::BeginDestroy();
}

PAttributeSet::FAttributeAddresses PAttributeSet::FindAttribute(EGameplayAttribute Attribute)
{
    switch (Attribute)
    {
    case EGameplayAttribute::Health: return {&BaseHealth, &Health};
    case EGameplayAttribute::MaxHealth: return {&BaseMaxHealth, &MaxHealth};
    case EGameplayAttribute::Mana: return {&BaseMana, &Mana};
    case EGameplayAttribute::MoveSpeed: return {&BaseMoveSpeed, &MoveSpeed};
    }
    return {};
}

PAttributeSet::FConstAttributeAddresses PAttributeSet::FindAttribute(
    EGameplayAttribute Attribute) const
{
    switch (Attribute)
    {
    case EGameplayAttribute::Health: return {&BaseHealth, &Health};
    case EGameplayAttribute::MaxHealth: return {&BaseMaxHealth, &MaxHealth};
    case EGameplayAttribute::Mana: return {&BaseMana, &Mana};
    case EGameplayAttribute::MoveSpeed: return {&BaseMoveSpeed, &MoveSpeed};
    }
    return {};
}

float PAttributeSet::ClampValue(EGameplayAttribute Attribute, float Value) const
{
    switch (Attribute)
    {
    case EGameplayAttribute::Health:
        return std::clamp(Value, 0.0f, std::max(0.0f, MaxHealth));
    case EGameplayAttribute::MaxHealth:
    case EGameplayAttribute::Mana:
    case EGameplayAttribute::MoveSpeed:
        return std::max(0.0f, Value);
    }
    return Value;
}

void PAttributeSet::SanitizeValues()
{
    if (!std::isfinite(BaseMaxHealth)) BaseMaxHealth = 100.0f;
    if (!std::isfinite(MaxHealth)) MaxHealth = BaseMaxHealth;
    BaseMaxHealth = std::max(0.0f, BaseMaxHealth);
    MaxHealth = std::max(0.0f, MaxHealth);

    if (!std::isfinite(BaseHealth)) BaseHealth = BaseMaxHealth;
    if (!std::isfinite(Health)) Health = MaxHealth;
    BaseHealth = std::clamp(BaseHealth, 0.0f, BaseMaxHealth);
    Health = std::clamp(Health, 0.0f, MaxHealth);

    if (!std::isfinite(BaseMana)) BaseMana = 100.0f;
    if (!std::isfinite(Mana)) Mana = BaseMana;
    BaseMana = std::max(0.0f, BaseMana);
    Mana = std::max(0.0f, Mana);

    if (!std::isfinite(BaseMoveSpeed)) BaseMoveSpeed = 600.0f;
    if (!std::isfinite(MoveSpeed)) MoveSpeed = BaseMoveSpeed;
    BaseMoveSpeed = std::max(0.0f, BaseMoveSpeed);
    MoveSpeed = std::max(0.0f, MoveSpeed);
}

void PAttributeSet::BroadcastChange(
    EGameplayAttribute Attribute,
    FGameplayAttributeData OldValue,
    FName Source)
{
    const FGameplayAttributeData NewValue = GetAttributeData(Attribute);
    if (OldValue.BaseValue == NewValue.BaseValue
        && OldValue.CurrentValue == NewValue.CurrentValue)
    {
        return;
    }
    AttributeChangedEvent.Broadcast(
        FGameplayAttributeChangeData {Attribute, OldValue, NewValue, Source});
}
}
