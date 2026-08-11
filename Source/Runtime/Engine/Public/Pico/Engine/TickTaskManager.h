#pragma once

#include "Pico/Core/Types.h"

#include <vector>

namespace Pico
{
class FTickFunction;
class PObject;
enum class ETickGroup : uint8;

class FTickTaskManager
{
public:
    bool RegisterTickFunction(FTickFunction& TickFunction, PObject* Owner);
    void UnregisterTickFunction(FTickFunction& TickFunction);
    void Tick(float DeltaSeconds);
    bool BeginFrame(float DeltaSeconds);
    bool RunTickGroup(ETickGroup Group);
    void EndFrame();
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
    uint64 FrameRegistrationLimit = 0;
    float FrameDeltaSeconds = 0.0f;
    int LastCompletedGroup = -1;
    bool bTicking = false;
};
}
