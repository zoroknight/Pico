#include "Pico/Tests/GCNode.h"

#include "Pico/Object/ReferenceCollector.h"
#include "Pico/Object/GarbageCollection.h"

namespace PicoTest
{
int PGCNode::DestroyedInstanceCount = 0;
bool PGCNode::bProbeReentrancy = false;
bool PGCNode::bSawGCInBeginDestroy = false;
bool PGCNode::bNestedCollectionRejected = false;

PGCNode::PGCNode(const Pico::FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PGCNode::SetStrong(PGCNode* Object)
{
    Strong = Object;
}

void PGCNode::SetWeak(PGCNode* Object)
{
    Weak = Object;
}

void PGCNode::AddNativeStrong(PGCNode* Object)
{
    if (Object != nullptr)
    {
        NativeStrongHandles.push_back(Object->GetHandle());
    }
}

PGCNode* PGCNode::GetStrong() const
{
    return Strong.Get();
}

PGCNode* PGCNode::GetWeak() const
{
    return Weak.Get();
}

int PGCNode::GetDestroyedInstanceCount()
{
    return DestroyedInstanceCount;
}

void PGCNode::EnableReentrancyProbe()
{
    bProbeReentrancy = true;
    bSawGCInBeginDestroy = false;
    bNestedCollectionRejected = false;
}

bool PGCNode::WasGCActiveInBeginDestroy()
{
    return bSawGCInBeginDestroy;
}

bool PGCNode::WasNestedCollectionRejected()
{
    return bNestedCollectionRejected;
}

void PGCNode::AddReferencedObjects(Pico::FReferenceCollector& Collector) const
{
    PObject::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(NativeStrongHandles);
}

void PGCNode::BeginDestroy()
{
    if (bProbeReentrancy)
    {
        bProbeReentrancy = false;
        bSawGCInBeginDestroy = Pico::IsGarbageCollecting();
        bNestedCollectionRejected = !Pico::CollectGarbage().bSucceeded;
    }
    if (GetHandle().IsValid())
    {
        ++DestroyedInstanceCount;
    }
    PObject::BeginDestroy();
}
}
