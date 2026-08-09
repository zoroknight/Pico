#include "Pico/Object/GarbageCollection.h"

#include "Pico/Object/ObjectRegistry.h"

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
    return FObjectRegistry::AddToRoot(Object);
}

bool RemoveFromRoot(PObject* Object)
{
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
    GetRequestedReasons() = EGarbageCollectionReason::None;
}

FGarbageCollectionResult CollectGarbage()
{
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
