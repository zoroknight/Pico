#include "Pico/Samples/DemoCharacter.h"

#include <algorithm>
namespace Pico
{
int32 PDemoCharacter::GetHealth() const
{
    return Health;
}

float PDemoCharacter::GetMoveSpeed() const
{
    return MoveSpeed;
}

bool PDemoCharacter::IsAlive() const
{
    return bAlive;
}

int32 PDemoCharacter::GetHealthSeenInPostLoad() const
{
    return HealthSeenInPostLoad;
}

int32 PDemoCharacter::ApplyDamage(int32 Damage)
{
    const int32 OldHealth = Health;
    Health = std::max(int32 { 0 }, Health - std::max(int32 { 0 }, Damage));
    bAlive = Health > 0;
    if (Health != OldHealth)
    {
        OnHealthChanged.Broadcast(OldHealth, Health);
    }
    return Health;
}

PDemoCharacter::PDemoCharacter(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PDemoCharacter::PostLoad()
{
    HealthSeenInPostLoad = Health;
}

PDemoHealthObserver::PDemoHealthObserver(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PDemoHealthObserver::HandleHealthChanged(int32 OldHealth, int32 NewHealth)
{
    ++NotificationCount;
    LastOldHealth = OldHealth;
    LastNewHealth = NewHealth;
}

int32 PDemoHealthObserver::GetNotificationCount() const
{
    return NotificationCount;
}

int32 PDemoHealthObserver::GetLastOldHealth() const
{
    return LastOldHealth;
}

int32 PDemoHealthObserver::GetLastNewHealth() const
{
    return LastNewHealth;
}
}
