#pragma once

#include <chrono>
#include <array>
#include <cstddef>

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

struct FFrameTimeStatistics
{
    double P50Milliseconds = 0.0;
    double P95Milliseconds = 0.0;
    double P99Milliseconds = 0.0;
    std::uint64_t LongFrameCount = 0;
    std::size_t SampleCount = 0;
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
    FFrameTimeStatistics GetStatistics() const;

private:
    using FClock = std::chrono::steady_clock;

    FClock::time_point StartTime = FClock::now();
    FClock::time_point LastTime = StartTime;
    FClock::time_point FrameStartTime = StartTime;
    double DeltaSeconds = 0.0;
    double TotalSeconds = 0.0;
    double AverageFrameTimeMS = 0.0;
    double AverageFPS = 0.0;
    static constexpr std::size_t SampleCapacity = 240;
    std::array<double, SampleCapacity> FrameSamplesMS {};
    std::size_t FrameSampleCount = 0;
    std::size_t NextFrameSample = 0;
    std::uint64_t LongFrameCount = 0;
};
}
