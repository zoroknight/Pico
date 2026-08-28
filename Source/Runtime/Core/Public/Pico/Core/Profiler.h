#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Pico
{
struct FProfileEvent
{
    std::uint64_t Id = 0;
    std::uint64_t ParentId = 0;
    std::uint64_t FrameId = 0;
    std::uint64_t ThreadId = 0;
    std::uint64_t StartMicroseconds = 0;
    std::uint64_t DurationMicroseconds = 0;
    std::uint32_t Depth = 0;
    std::string Name;
};

struct FProfileAggregate
{
    std::string Name;
    std::uint64_t Count = 0;
    std::uint64_t TotalMicroseconds = 0;
    std::uint64_t MinMicroseconds = 0;
    std::uint64_t MaxMicroseconds = 0;
};

struct FProfileScopeToken
{
    std::uint64_t Id = 0;
    std::uint64_t ParentId = 0;
    std::uint64_t FrameId = 0;
    std::uint64_t ThreadId = 0;
    std::uint64_t StartMicroseconds = 0;
    std::uint32_t Depth = 0;
    const char* Name = nullptr;

    explicit operator bool() const { return Id != 0; }
};

class FProfiler
{
public:
    static FProfiler& Get();
    ~FProfiler();

    FProfiler(const FProfiler&) = delete;
    FProfiler& operator=(const FProfiler&) = delete;

    void SetEnabled(bool bInEnabled);
    bool IsEnabled() const;
    void BeginFrame();
    void EndFrame();
    FProfileScopeToken BeginScope(const char* Name);
    void EndScope(FProfileScopeToken& Token);
    void Reset();

    std::vector<FProfileEvent> GetEvents() const;
    std::vector<FProfileAggregate> GetAggregates() const;
    bool WriteChromeTrace(
        const std::filesystem::path& Path,
        std::string* OutError = nullptr) const;
    bool WriteSummaryJson(
        const std::filesystem::path& Path,
        std::string* OutError = nullptr) const;

private:
    FProfiler();
    std::uint64_t NowMicroseconds() const;

    struct FImpl;
    FImpl* Impl = nullptr;
    std::atomic<bool> bEnabled {false};
    std::atomic<std::uint64_t> CurrentFrameId {0};
    std::atomic<std::uint64_t> NextEventId {1};
};

class FProfileScope
{
public:
    explicit FProfileScope(const char* Name)
    {
        FProfiler& Profiler = FProfiler::Get();
        if (Profiler.IsEnabled()) Token = Profiler.BeginScope(Name);
    }

    ~FProfileScope()
    {
        if (Token) FProfiler::Get().EndScope(Token);
    }

    FProfileScope(const FProfileScope&) = delete;
    FProfileScope& operator=(const FProfileScope&) = delete;

private:
    FProfileScopeToken Token;
};
}

#define PICO_PROFILE_JOIN_INNER(A, B) A##B
#define PICO_PROFILE_JOIN(A, B) PICO_PROFILE_JOIN_INNER(A, B)
#define PICO_PROFILE_SCOPE(Name) \
    ::Pico::FProfileScope PICO_PROFILE_JOIN(PicoProfileScope_, __LINE__)(Name)
#define PICO_PROFILE_FUNCTION() PICO_PROFILE_SCOPE(__func__)
