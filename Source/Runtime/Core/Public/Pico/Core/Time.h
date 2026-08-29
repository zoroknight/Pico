#pragma once

#include <chrono>

namespace Pico
{
enum class EFramePacingMode
{
    VSync,
    Software,
    Unlimited
};

const char* ToString(EFramePacingMode Mode);

struct FFramePacingSettings
{
    EFramePacingMode ResolveMode(bool bPresentationCanVSync) const;

    bool bVSync = true;
    double MaxFPS = 60.0;
};

class FFrameTimer
{
public:
    void Reset();
    void Tick();
    void WaitForMaxFPS(double MaxFPS);

    double GetDeltaSeconds() const;
    double GetTotalSeconds() const;
    double GetAverageFrameTimeMS() const;
    double GetAverageFPS() const;

private:
    using FClock = std::chrono::steady_clock;

    FClock::time_point StartTime = FClock::now();
    FClock::time_point LastTime = StartTime;
    FClock::time_point FrameStartTime = StartTime;
    double DeltaSeconds = 0.0;
    double TotalSeconds = 0.0;
    double AverageFrameTimeMS = 0.0;
    double AverageFPS = 0.0;
};
}
