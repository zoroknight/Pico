#pragma once

#include "Pico/Core/Delegate.h"
#include "Pico/Core/Name.h"
#include "Pico/Object/ReflectionMacros.h"

#include <string>
#include <string_view>

namespace Pico
{
enum class EGameplayAttribute : uint8
{
    Health,
    MaxHealth,
    Mana,
    MoveSpeed
};

std::string_view ToString(EGameplayAttribute Attribute);
bool TryParseGameplayAttribute(std::string_view Name, EGameplayAttribute& OutAttribute);

struct FGameplayAttributeData
{
    float BaseValue = 0.0f;
    float CurrentValue = 0.0f;
};

struct FGameplayAttributeChangeData
{
    EGameplayAttribute Attribute = EGameplayAttribute::Health;
    FGameplayAttributeData OldValue;
    FGameplayAttributeData NewValue;
    FName Source;
};

using FOnGameplayAttributeChanged =
    TMulticastDelegate<void(const FGameplayAttributeChangeData&)>;

class PAttributeSet : public PObject
{
    PICO_DECLARE_CLASS(PAttributeSet, PObject)

public:
    FGameplayAttributeData GetAttributeData(EGameplayAttribute Attribute) const;
    float GetBaseValue(EGameplayAttribute Attribute) const;
    float GetCurrentValue(EGameplayAttribute Attribute) const;

    bool SetBaseValue(EGameplayAttribute Attribute, float Value, FName Source = {});
    bool SetCurrentValue(EGameplayAttribute Attribute, float Value, FName Source = {});
    bool ModifyCurrentValue(EGameplayAttribute Attribute, float Delta, FName Source = {});

    FOnGameplayAttributeChanged& OnAttributeChanged();
    const FOnGameplayAttributeChanged& OnAttributeChanged() const;
    std::string GetDebugDescription() const;

protected:
    explicit PAttributeSet(const FObjectConstructionParams& Params);
    void PostLoad() override;
    void BeginDestroy() override;

private:
    struct FAttributeAddresses
    {
        float* Base = nullptr;
        float* Current = nullptr;
    };
    struct FConstAttributeAddresses
    {
        const float* Base = nullptr;
        const float* Current = nullptr;
    };

    FAttributeAddresses FindAttribute(EGameplayAttribute Attribute);
    FConstAttributeAddresses FindAttribute(EGameplayAttribute Attribute) const;
    float ClampValue(EGameplayAttribute Attribute, float Value) const;
    void SanitizeValues();
    void BroadcastChange(
        EGameplayAttribute Attribute,
        FGameplayAttributeData OldValue,
        FName Source);

    float BaseHealth = 100.0f;
    float Health = 100.0f;
    float BaseMaxHealth = 100.0f;
    float MaxHealth = 100.0f;
    float BaseMana = 100.0f;
    float Mana = 100.0f;
    float BaseMoveSpeed = 600.0f;
    float MoveSpeed = 600.0f;
    FOnGameplayAttributeChanged AttributeChangedEvent;
};
}
