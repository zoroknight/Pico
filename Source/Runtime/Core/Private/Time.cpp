#include "Pico/Core/Time.h"

#include <thread>

namespace Pico
{
void FFrameTimer::Reset()
{
    StartTime = FClock::now();
    LastTime = StartTime;
    FrameStartTime = StartTime;
    DeltaSeconds = 0.0;
    TotalSeconds = 0.0;
    AverageFrameTimeMS = 0.0;
    AverageFPS = 0.0;
}

void FFrameTimer::Tick()
{
    const FClock::time_point Now = FClock::now();
    DeltaSeconds = std::chrono::duration<double>(Now - LastTime).count();
    TotalSeconds = std::chrono::duration<double>(Now - StartTime).count();
    LastTime = Now;
    FrameStartTime = Now;

    const double FrameTimeMS = DeltaSeconds * 1000.0;
    AverageFrameTimeMS = AverageFrameTimeMS <= 0.0
        ? FrameTimeMS
        : AverageFrameTimeMS * 0.9 + FrameTimeMS * 0.1;

    AverageFPS = AverageFrameTimeMS > 0.0 ? 1000.0 / AverageFrameTimeMS : 0.0;
}

void FFrameTimer::WaitForMaxFPS(double MaxFPS)
{
    if (MaxFPS <= 0.0)
    {
        return;
    }

    const double TargetFrameSeconds = 1.0 / MaxFPS;
    const FClock::time_point Now = FClock::now();
    const double ElapsedSeconds = std::chrono::duration<double>(Now - FrameStartTime).count();
    const double RemainingSeconds = TargetFrameSeconds - ElapsedSeconds;

    if (RemainingSeconds > 0.0)
    {
        std::this_thread::sleep_for(std::chrono::duration<double>(RemainingSeconds));
    }
}

double FFrameTimer::GetDeltaSeconds() const
{
    return DeltaSeconds;
}

double FFrameTimer::GetTotalSeconds() const
{
    return TotalSeconds;
}

double FFrameTimer::GetAverageFrameTimeMS() const
{
    return AverageFrameTimeMS;
}

double FFrameTimer::GetAverageFPS() const
{
    return AverageFPS;
}
}
