#pragma once

#include "Pico/Object/Object.h"

namespace Pico
{
class PClass;

class PDemoCharacter final : public PObject
{
public:
    static const PClass* StaticClass();
    static bool RegisterClass();

    int32 GetHealth() const;
    float GetMoveSpeed() const;
    bool IsAlive() const;
    int32 GetHealthSeenInPostLoad() const;

protected:
    explicit PDemoCharacter(const FObjectConstructionParams& Params);
    void PostLoad() override;

private:
    static FObjectPtr ConstructInstance(const FObjectConstructionParams& Params);
    static bool AddProperties(PClass& Class);

    int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
    int32 HealthSeenInPostLoad = 0;
};
}
