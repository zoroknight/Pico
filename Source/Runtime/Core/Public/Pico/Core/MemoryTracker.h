#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace Pico
{
enum class EMemoryTag : std::uint8_t
{
    ObjectSlots,
    ObjectNameIndex,
    ObjectHierarchyIndex,
    GCScratch,
    ProfilerEvents,
    ReplicationSchema,
    ReplicationChannels,
    Count
};

struct FMemorySnapshot
{
    EMemoryTag Tag = EMemoryTag::ObjectSlots;
    std::uint64_t CurrentBytes = 0;
    std::uint64_t ReservedBytes = 0;
    std::uint64_t PeakBytes = 0;
    std::uint64_t ElementCount = 0;
    std::uint64_t GrowthCount = 0;
};

std::string_view GetMemoryTagName(EMemoryTag Tag);

class FMemoryTracker
{
public:
    static FMemoryTracker& Get();

    FMemoryTracker(const FMemoryTracker&) = delete;
    FMemoryTracker& operator=(const FMemoryTracker&) = delete;

    void SetEnabled(bool bInEnabled);
    bool IsEnabled() const;
    void Report(
        EMemoryTag Tag,
        std::uint64_t CurrentBytes,
        std::uint64_t ReservedBytes,
        std::uint64_t ElementCount);
    FMemorySnapshot GetSnapshot(EMemoryTag Tag) const;
    std::vector<FMemorySnapshot> GetSnapshots() const;
    void Reset();

private:
    FMemoryTracker() = default;

    static constexpr std::size_t TagCount =
        static_cast<std::size_t>(EMemoryTag::Count);
    mutable std::mutex Mutex;
    std::array<FMemorySnapshot, TagCount> Snapshots {};
    std::atomic<bool> bEnabled {false};
};
}
