#pragma once

#include <cstddef>
#include <cstdint>

namespace Pico
{
class PObject;

enum class EGarbageCollectionReason : std::uint32_t
{
    None = 0,
    Explicit = 1u << 0,
    TimeLimit = 1u << 1,
    WorldTransition = 1u << 2,
    EngineExit = 1u << 3
};

constexpr EGarbageCollectionReason operator|(
    EGarbageCollectionReason Left,
    EGarbageCollectionReason Right)
{
    return static_cast<EGarbageCollectionReason>(
        static_cast<std::uint32_t>(Left) | static_cast<std::uint32_t>(Right));
}

constexpr bool HasAnyGarbageCollectionReason(
    EGarbageCollectionReason Value,
    EGarbageCollectionReason Reasons)
{
    return (static_cast<std::uint32_t>(Value)
        & static_cast<std::uint32_t>(Reasons)) != 0;
}

struct FGarbageCollectionResult
{
    std::size_t ObjectCountBefore = 0;
    std::size_t RootCount = 0;
    std::size_t ReachableObjectCount = 0;
    std::size_t CollectedObjectCount = 0;
    std::size_t ObjectCountAfter = 0;
    std::uint64_t RootScanNanoseconds = 0;
    std::uint64_t MarkNanoseconds = 0;
    std::uint64_t UnreachableSortNanoseconds = 0;
    std::uint64_t DestroyNanoseconds = 0;
    std::size_t StrongReferenceLayoutCount = 0;
    std::size_t StrongReferencePropertyVisitCount = 0;
    std::size_t ScratchPeakBytes = 0;
    std::size_t ScratchReservedBytes = 0;
    std::uint64_t ScratchGrowthCount = 0;
    bool bSucceeded = false;
};

bool AddToRoot(PObject* Object);
bool RemoveFromRoot(PObject* Object);
bool IsRooted(const PObject* Object);
bool IsGarbageCollecting();
void RequestGarbageCollection(
    EGarbageCollectionReason Reason = EGarbageCollectionReason::Explicit);
bool IsGarbageCollectionRequested();
EGarbageCollectionReason GetPendingGarbageCollectionReasons();
bool CollectGarbageIfRequested(FGarbageCollectionResult* OutResult = nullptr);
void ResetGarbageCollectionRequests();
FGarbageCollectionResult CollectGarbage();
}
