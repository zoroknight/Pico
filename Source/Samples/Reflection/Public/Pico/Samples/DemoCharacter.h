#pragma once

#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ReflectionMacros.h"

namespace Pico
{
class PDemoCharacter final : public PObject
{
    PICO_DECLARE_CLASS(PDemoCharacter, PObject)

public:
    int32 GetHealth() const;
    float GetMoveSpeed() const;
    bool IsAlive() const;
    int32 GetHealthSeenInPostLoad() const;
    int32 ApplyDamage(int32 Damage);

    TObjectMulticastDelegate<void(int32, int32)> OnHealthChanged;

protected:
    explicit PDemoCharacter(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
    int32 HealthSeenInPostLoad = 0;
};

class PDemoHealthObserver final : public PObject
{
    PICO_DECLARE_CLASS(PDemoHealthObserver, PObject)

public:
    void HandleHealthChanged(int32 OldHealth, int32 NewHealth);
    int32 GetNotificationCount() const;
    int32 GetLastOldHealth() const;
    int32 GetLastNewHealth() const;

protected:
    explicit PDemoHealthObserver(const FObjectConstructionParams& Params);

private:
    int32 NotificationCount = 0;
    int32 LastOldHealth = 0;
    int32 LastNewHealth = 0;
};
}
