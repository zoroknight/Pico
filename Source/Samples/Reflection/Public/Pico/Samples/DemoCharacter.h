#pragma once

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

protected:
    explicit PDemoCharacter(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
    int32 HealthSeenInPostLoad = 0;
};
}
