#include "Pico/Core/Time.h"

#include "Pico/Core/Platform.h"

#include <algorithm>
#include <vector>
#include <thread>

#if PICO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Pico
{
namespace
{
using FClock = std::chrono::steady_clock;

void SleepForFramePacing(double Seconds)
{
    if (Seconds <= 0.0)
    {
        return;
    }
#if PICO_PLATFORM_WINDOWS
    struct FThreadWaitableTimer
    {
        FThreadWaitableTimer()
        {
#if defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
            Handle = CreateWaitableTimerExW(
                nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                TIMER_MODIFY_STATE | SYNCHRONIZE);
#endif
            if (Handle == nullptr)
            {
                Handle = CreateWaitableTimerExW(
                    nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
            }
        }

        ~FThreadWaitableTimer()
        {
            if (Handle != nullptr)
            {
                CloseHandle(Handle);
            }
        }

        HANDLE Handle = nullptr;
    };

    static thread_local FThreadWaitableTimer Timer;
    if (Timer.Handle != nullptr)
    {
        LARGE_INTEGER DueTime {};
        DueTime.QuadPart = -std::max<LONGLONG>(
            1, static_cast<LONGLONG>(Seconds * 10000000.0));
        if (SetWaitableTimer(
                Timer.Handle, &DueTime, 0, nullptr, nullptr, FALSE)
            && WaitForSingleObject(Timer.Handle, INFINITE) == WAIT_OBJECT_0)
        {
            return;
        }
    }
#endif
    std::this_thread::sleep_for(std::chrono::duration<double>(Seconds));
}
}

const char* ToString(EFramePacingMode Mode)
{
    switch (Mode)
    {
    case EFramePacingMode::VSync: return "VSync";
    case EFramePacingMode::Software: return "Software";
    case EFramePacingMode::Unlimited: return "Unlimited";
    }
    return "Unknown";
}

EFramePacingMode FFramePacingSettings::ResolveMode(
    bool bPresentationCanVSync) const
{
    if (bVSync && bPresentationCanVSync)
    {
        return EFramePacingMode::VSync;
    }
    return MaxFPS > 0.0
        ? EFramePacingMode::Software
        : EFramePacingMode::Unlimited;
}

void FFrameTimer::Reset()
{
    StartTime = FClock::now();
    LastTime = StartTime;
    FrameStartTime = StartTime;
    DeltaSeconds = 0.0;
    TotalSeconds = 0.0;
    AverageFrameTimeMS = 0.0;
    AverageFPS = 0.0;
    FrameSamplesMS.fill(0.0);
    FrameSampleCount = 0;
    NextFrameSample = 0;
    LongFrameCount = 0;
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
    FrameSamplesMS[NextFrameSample] = FrameTimeMS;
    NextFrameSample = (NextFrameSample + 1) % SampleCapacity;
    FrameSampleCount = std::min(FrameSampleCount + 1, SampleCapacity);
    if (FrameTimeMS > 16.67) ++LongFrameCount;
}

void FFrameTimer::WaitForMaxFPS(double MaxFPS)
{
    if (MaxFPS <= 0.0)
    {
        return;
    }

    const double TargetFrameSeconds = 1.0 / MaxFPS;
    constexpr double SpinTailSeconds = 0.0005;
    const FClock::time_point Deadline = FrameStartTime
        + std::chrono::duration_cast<FClock::duration>(
            std::chrono::duration<double>(TargetFrameSeconds));
    const FClock::time_point Now = FClock::now();
    const double RemainingSeconds =
        std::chrono::duration<double>(Deadline - Now).count();

    if (RemainingSeconds > SpinTailSeconds)
    {
        SleepForFramePacing(RemainingSeconds - SpinTailSeconds);
    }
    while (FClock::now() < Deadline)
    {
        std::this_thread::yield();
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

FFrameTimeStatistics FFrameTimer::GetStatistics() const
{
    FFrameTimeStatistics Result;
    Result.LongFrameCount = LongFrameCount;
    Result.SampleCount = FrameSampleCount;
    if (FrameSampleCount == 0) return Result;
    std::vector<double> Sorted(
        FrameSamplesMS.begin(), FrameSamplesMS.begin() + FrameSampleCount);
    std::sort(Sorted.begin(), Sorted.end());
    const auto Percentile = [&Sorted](double Value)
    {
        const std::size_t Index = static_cast<std::size_t>(
            Value * static_cast<double>(Sorted.size() - 1));
        return Sorted[Index];
    };
    Result.P50Milliseconds = Percentile(0.50);
    Result.P95Milliseconds = Percentile(0.95);
    Result.P99Milliseconds = Percentile(0.99);
    return Result;
}
}
