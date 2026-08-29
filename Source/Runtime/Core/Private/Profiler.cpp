#include "Pico/Core/Profiler.h"

#include "Pico/Core/MemoryTracker.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace Pico
{
namespace
{
thread_local std::vector<std::uint64_t> ActiveScopeIds;

std::string EscapeJson(const std::string& Value)
{
    std::ostringstream Stream;
    for (const unsigned char Character : Value)
    {
        switch (Character)
        {
        case '\"': Stream << "\\\""; break;
        case '\\': Stream << "\\\\"; break;
        case '\b': Stream << "\\b"; break;
        case '\f': Stream << "\\f"; break;
        case '\n': Stream << "\\n"; break;
        case '\r': Stream << "\\r"; break;
        case '\t': Stream << "\\t"; break;
        default:
            if (Character < 0x20)
            {
                static constexpr char Hex[] = "0123456789abcdef";
                Stream << "\\u00" << Hex[Character >> 4] << Hex[Character & 0xf];
            }
            else Stream << Character;
            break;
        }
    }
    return Stream.str();
}

template <typename TWriter>
bool WriteAtomically(
    const std::filesystem::path& Path,
    TWriter&& Writer,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    try
    {
        if (!Path.parent_path().empty())
            std::filesystem::create_directories(Path.parent_path());
        const std::filesystem::path StagingPath = Path.string() + ".tmp";
        {
            std::ofstream Stream(StagingPath, std::ios::binary | std::ios::trunc);
            if (!Stream) throw std::runtime_error("Could not open profiler staging file");
            Writer(Stream);
            Stream.flush();
            if (!Stream) throw std::runtime_error("Could not flush profiler report");
        }
        std::error_code ErrorCode;
        std::filesystem::remove(Path, ErrorCode);
        std::filesystem::rename(StagingPath, Path);
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}
}

struct FProfiler::FImpl
{
    std::chrono::steady_clock::time_point Origin = std::chrono::steady_clock::now();
    mutable std::mutex Mutex;
    std::vector<FProfileEvent> Events;
};

FProfiler::FProfiler()
    : Impl(new FImpl())
{
}

FProfiler::~FProfiler()
{
    delete Impl;
}

FProfiler& FProfiler::Get()
{
    static FProfiler Instance;
    return Instance;
}

void FProfiler::SetEnabled(bool bInEnabled)
{
    bEnabled.store(bInEnabled, std::memory_order_release);
    if (!bInEnabled) ActiveScopeIds.clear();
}

bool FProfiler::IsEnabled() const
{
    return bEnabled.load(std::memory_order_relaxed);
}

void FProfiler::BeginFrame()
{
    if (!IsEnabled()) return;
    CurrentFrameId.fetch_add(1, std::memory_order_relaxed);
}

void FProfiler::EndFrame()
{
    if (!FMemoryTracker::Get().IsEnabled()) return;
    std::lock_guard Lock(Impl->Mutex);
    FMemoryTracker::Get().Report(EMemoryTag::ProfilerEvents,
        Impl->Events.size() * sizeof(FProfileEvent),
        Impl->Events.capacity() * sizeof(FProfileEvent),
        Impl->Events.size());
}

FProfileScopeToken FProfiler::BeginScope(const char* Name)
{
    FProfileScopeToken Token;
    if (!IsEnabled() || Name == nullptr) return Token;
    Token.Id = NextEventId.fetch_add(1, std::memory_order_relaxed);
    Token.ParentId = ActiveScopeIds.empty() ? 0 : ActiveScopeIds.back();
    Token.FrameId = CurrentFrameId.load(std::memory_order_relaxed);
    Token.ThreadId = static_cast<std::uint64_t>(
        std::hash<std::thread::id> {}(std::this_thread::get_id()));
    Token.StartMicroseconds = NowMicroseconds();
    Token.Depth = static_cast<std::uint32_t>(ActiveScopeIds.size());
    Token.Name = Name;
    ActiveScopeIds.push_back(Token.Id);
    return Token;
}

void FProfiler::EndScope(FProfileScopeToken& Token)
{
    if (!Token) return;
    const std::uint64_t EndMicroseconds = NowMicroseconds();
    if (!ActiveScopeIds.empty() && ActiveScopeIds.back() == Token.Id)
        ActiveScopeIds.pop_back();
    else
    {
        const auto Found = std::find(
            ActiveScopeIds.begin(), ActiveScopeIds.end(), Token.Id);
        if (Found != ActiveScopeIds.end()) ActiveScopeIds.erase(Found);
    }

    FProfileEvent Event;
    Event.Id = Token.Id;
    Event.ParentId = Token.ParentId;
    Event.FrameId = Token.FrameId;
    Event.ThreadId = Token.ThreadId;
    Event.StartMicroseconds = Token.StartMicroseconds;
    Event.DurationMicroseconds = EndMicroseconds >= Token.StartMicroseconds
        ? EndMicroseconds - Token.StartMicroseconds : 0;
    Event.Depth = Token.Depth;
    Event.Name = Token.Name;
    {
        std::lock_guard Lock(Impl->Mutex);
        const std::size_t PreviousCapacity = Impl->Events.capacity();
        Impl->Events.push_back(std::move(Event));
        if (Impl->Events.capacity() != PreviousCapacity)
        {
            FMemoryTracker::Get().Report(EMemoryTag::ProfilerEvents,
                Impl->Events.size() * sizeof(FProfileEvent),
                Impl->Events.capacity() * sizeof(FProfileEvent),
                Impl->Events.size());
        }
    }
    Token = {};
}

void FProfiler::Reset()
{
    std::lock_guard Lock(Impl->Mutex);
    Impl->Events.clear();
    FMemoryTracker::Get().Report(EMemoryTag::ProfilerEvents,
        0, Impl->Events.capacity() * sizeof(FProfileEvent), 0);
    Impl->Origin = std::chrono::steady_clock::now();
    CurrentFrameId.store(0, std::memory_order_relaxed);
    NextEventId.store(1, std::memory_order_relaxed);
    ActiveScopeIds.clear();
}

std::vector<FProfileEvent> FProfiler::GetEvents() const
{
    std::lock_guard Lock(Impl->Mutex);
    return Impl->Events;
}

std::vector<FProfileAggregate> FProfiler::GetAggregates() const
{
    std::map<std::string, FProfileAggregate> ByName;
    for (const FProfileEvent& Event : GetEvents())
    {
        FProfileAggregate& Aggregate = ByName[Event.Name];
        Aggregate.Name = Event.Name;
        ++Aggregate.Count;
        Aggregate.TotalMicroseconds += Event.DurationMicroseconds;
        Aggregate.MinMicroseconds = Aggregate.Count == 1
            ? Event.DurationMicroseconds
            : std::min(Aggregate.MinMicroseconds, Event.DurationMicroseconds);
        Aggregate.MaxMicroseconds = std::max(
            Aggregate.MaxMicroseconds, Event.DurationMicroseconds);
    }
    std::vector<FProfileAggregate> Result;
    Result.reserve(ByName.size());
    for (auto& [Name, Aggregate] : ByName)
    {
        (void)Name;
        Result.push_back(std::move(Aggregate));
    }
    return Result;
}

bool FProfiler::WriteChromeTrace(
    const std::filesystem::path& Path,
    std::string* OutError) const
{
    std::vector<FProfileEvent> Events = GetEvents();
    std::sort(Events.begin(), Events.end(),
        [](const FProfileEvent& Left, const FProfileEvent& Right)
        {
            return Left.StartMicroseconds != Right.StartMicroseconds
                ? Left.StartMicroseconds < Right.StartMicroseconds
                : Left.Id < Right.Id;
        });
    return WriteAtomically(Path, [&Events](std::ostream& Stream)
    {
        Stream << "{\n  \"displayTimeUnit\": \"ms\",\n  \"traceEvents\": [\n";
        for (std::size_t Index = 0; Index < Events.size(); ++Index)
        {
            const FProfileEvent& Event = Events[Index];
            Stream << "    {\"name\":\"" << EscapeJson(Event.Name)
                << "\",\"cat\":\"Pico\",\"ph\":\"X\",\"pid\":1,\"tid\":"
                << Event.ThreadId << ",\"ts\":" << Event.StartMicroseconds
                << ",\"dur\":" << Event.DurationMicroseconds
                << ",\"args\":{\"frame\":" << Event.FrameId
                << ",\"scope_id\":" << Event.Id
                << ",\"parent_id\":" << Event.ParentId
                << ",\"depth\":" << Event.Depth << "}}";
            if (Index + 1 < Events.size()) Stream << ',';
            Stream << '\n';
        }
        Stream << "  ]\n}\n";
    }, OutError);
}

bool FProfiler::WriteSummaryJson(
    const std::filesystem::path& Path,
    std::string* OutError) const
{
    const std::vector<FProfileAggregate> Aggregates = GetAggregates();
    return WriteAtomically(Path, [&Aggregates](std::ostream& Stream)
    {
        Stream << "{\n  \"format_version\": 1,\n  \"scopes\": [\n";
        for (std::size_t Index = 0; Index < Aggregates.size(); ++Index)
        {
            const FProfileAggregate& Aggregate = Aggregates[Index];
            Stream << "    {\"name\":\"" << EscapeJson(Aggregate.Name)
                << "\",\"count\":" << Aggregate.Count
                << ",\"total_us\":" << Aggregate.TotalMicroseconds
                << ",\"min_us\":" << Aggregate.MinMicroseconds
                << ",\"max_us\":" << Aggregate.MaxMicroseconds << '}';
            if (Index + 1 < Aggregates.size()) Stream << ',';
            Stream << '\n';
        }
        Stream << "  ]\n}\n";
    }, OutError);
}

std::uint64_t FProfiler::NowMicroseconds() const
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - Impl->Origin).count());
}
}
