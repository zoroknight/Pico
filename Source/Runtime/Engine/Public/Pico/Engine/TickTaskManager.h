#pragma once

#include "Pico/Core/Types.h"

#include <vector>

namespace Pico
{
class FTickFunction;
class PObject;

class FTickTaskManager
{
public:
    bool RegisterTickFunction(FTickFunction& TickFunction, PObject* Owner);
    void UnregisterTickFunction(FTickFunction& TickFunction);
    void Tick(float DeltaSeconds);
    void Reset();
    std::size_t GetRegisteredTickFunctionCount() const;
    bool IsTicking() const;

private:
    struct FRegisteredTick
    {
        uint64 Id = 0;
        FTickFunction* Function = nullptr;
    };

    FRegisteredTick* FindRegisteredTick(uint64 Id);
    const FRegisteredTick* FindRegisteredTick(uint64 Id) const;
    void TickGroup(uint64 FrameRegistrationLimit, int GroupIndex, float DeltaSeconds);

    std::vector<FRegisteredTick> RegisteredTicks;
    uint64 NextRegistrationId = 1;
    bool bTicking = false;
};
}
