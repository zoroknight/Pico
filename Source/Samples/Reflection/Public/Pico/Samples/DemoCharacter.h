#pragma once

#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Samples/DemoCharacter.generated.h"

namespace Pico
{
PCLASS()
class PDemoCharacter final : public PObject
{
    GENERATED_BODY()

public:
    int32 GetHealth() const;
    float GetMoveSpeed() const;
    bool IsAlive() const;
    int32 GetHealthSeenInPostLoad() const;
    PFUNCTION(Callable)
    int32 ApplyDamage(int32 Damage);

    TObjectMulticastDelegate<void(int32, int32)> OnHealthChanged;

protected:
    explicit PDemoCharacter(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    PPROPERTY()
    int32 Health = 100;
    PPROPERTY()
    float MoveSpeed = 600.0f;
    PPROPERTY()
    bool bAlive = true;
    int32 HealthSeenInPostLoad = 0;
};

PCLASS()
class PDemoHealthObserver final : public PObject
{
    GENERATED_BODY()

public:
    PFUNCTION(Callable)
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
