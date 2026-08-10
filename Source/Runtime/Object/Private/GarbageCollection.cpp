#include "Pico/Object/GarbageCollection.h"

#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Core/GameThread.h"

namespace Pico
{
namespace
{
EGarbageCollectionReason& GetRequestedReasons()
{
    static EGarbageCollectionReason Reasons = EGarbageCollectionReason::None;
    return Reasons;
}
}

bool AddToRoot(PObject* Object)
{
    if (!CheckGameThread("AddToRoot")) return false;
    return FObjectRegistry::AddToRoot(Object);
}

bool RemoveFromRoot(PObject* Object)
{
    if (!CheckGameThread("RemoveFromRoot")) return false;
    return FObjectRegistry::RemoveFromRoot(Object);
}

bool IsRooted(const PObject* Object)
{
    return FObjectRegistry::IsRooted(Object);
}

bool IsGarbageCollecting()
{
    return FObjectRegistry::IsGarbageCollecting();
}

void RequestGarbageCollection(EGarbageCollectionReason Reason)
{
    if (!CheckGameThread("RequestGarbageCollection")) return;
    if (Reason != EGarbageCollectionReason::None)
    {
        GetRequestedReasons() = GetRequestedReasons() | Reason;
    }
}

bool IsGarbageCollectionRequested()
{
    return GetRequestedReasons() != EGarbageCollectionReason::None;
}

EGarbageCollectionReason GetPendingGarbageCollectionReasons()
{
    return GetRequestedReasons();
}

bool CollectGarbageIfRequested(FGarbageCollectionResult* OutResult)
{
    if (!CheckGameThread("CollectGarbageIfRequested")) return false;
    if (!IsGarbageCollectionRequested())
    {
        return false;
    }

    const FGarbageCollectionResult Result = CollectGarbage();
    if (OutResult != nullptr)
    {
        *OutResult = Result;
    }
    return Result.bSucceeded;
}

void ResetGarbageCollectionRequests()
{
    if (IsGameThreadInitialized() && !CheckGameThread("ResetGarbageCollectionRequests")) return;
    GetRequestedReasons() = EGarbageCollectionReason::None;
}

FGarbageCollectionResult CollectGarbage()
{
    if (!CheckGameThread("CollectGarbage")) return {};
    const EGarbageCollectionReason ReasonsAtStart = GetRequestedReasons();
    GetRequestedReasons() = EGarbageCollectionReason::None;
    FGarbageCollectionResult Result = FObjectRegistry::CollectGarbage();
    if (!Result.bSucceeded)
    {
        GetRequestedReasons() = GetRequestedReasons() | ReasonsAtStart;
    }
    return Result;
}
}
