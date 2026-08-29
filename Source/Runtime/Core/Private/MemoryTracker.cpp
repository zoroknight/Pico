#include "Pico/Core/MemoryTracker.h"

#include <algorithm>

namespace Pico
{
namespace
{
constexpr std::array<std::string_view,
    static_cast<std::size_t>(EMemoryTag::Count)> MemoryTagNames {
    "Object.Slots",
    "Object.NameIndex",
    "Object.HierarchyIndex",
    "GC.Scratch",
    "Profiler.Events",
    "Replication.Schema",
    "Replication.Channels"};
}

std::string_view GetMemoryTagName(EMemoryTag Tag)
{
    const std::size_t Index = static_cast<std::size_t>(Tag);
    return Index < MemoryTagNames.size()
        ? MemoryTagNames[Index] : std::string_view {"Unknown"};
}

FMemoryTracker& FMemoryTracker::Get()
{
    static FMemoryTracker Instance;
    return Instance;
}

void FMemoryTracker::SetEnabled(bool bInEnabled)
{
    bEnabled.store(bInEnabled, std::memory_order_release);
}

bool FMemoryTracker::IsEnabled() const
{
    return bEnabled.load(std::memory_order_relaxed);
}

void FMemoryTracker::Report(
    EMemoryTag Tag,
    std::uint64_t CurrentBytes,
    std::uint64_t ReservedBytes,
    std::uint64_t ElementCount)
{
    if (!IsEnabled()) return;
    const std::size_t Index = static_cast<std::size_t>(Tag);
    if (Index >= TagCount) return;

    std::lock_guard Lock(Mutex);
    FMemorySnapshot& Snapshot = Snapshots[Index];
    Snapshot.Tag = Tag;
    ReservedBytes = std::max(CurrentBytes, ReservedBytes);
    if (ReservedBytes > Snapshot.ReservedBytes) ++Snapshot.GrowthCount;
    Snapshot.CurrentBytes = CurrentBytes;
    Snapshot.ReservedBytes = ReservedBytes;
    Snapshot.PeakBytes = std::max(Snapshot.PeakBytes, CurrentBytes);
    Snapshot.ElementCount = ElementCount;
}

FMemorySnapshot FMemoryTracker::GetSnapshot(EMemoryTag Tag) const
{
    const std::size_t Index = static_cast<std::size_t>(Tag);
    if (Index >= TagCount) return {};
    std::lock_guard Lock(Mutex);
    return Snapshots[Index];
}

std::vector<FMemorySnapshot> FMemoryTracker::GetSnapshots() const
{
    std::lock_guard Lock(Mutex);
    return {Snapshots.begin(), Snapshots.end()};
}

void FMemoryTracker::Reset()
{
    std::lock_guard Lock(Mutex);
    for (std::size_t Index = 0; Index < Snapshots.size(); ++Index)
    {
        Snapshots[Index] = {};
        Snapshots[Index].Tag = static_cast<EMemoryTag>(Index);
    }
}
}
