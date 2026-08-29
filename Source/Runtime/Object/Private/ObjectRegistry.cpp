#include "Pico/Object/ObjectRegistry.h"

#include "Pico/Core/Log.h"
#include "Pico/Core/MemoryTracker.h"
#include "Pico/Core/ScopeExit.h"
#include "Pico/Core/Profiler.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectName.h"
#include "Pico/Object/ReferenceCollector.h"

#include <algorithm>
#include <exception>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Pico
{
namespace
{
struct FObjectSlot
{
    FObjectPtr Object;
    uint32 Serial = 0;
};

struct FObjectNameKey
{
    FObjectHandle OuterHandle;
    FName Name;

    friend bool operator==(const FObjectNameKey&, const FObjectNameKey&) = default;
};

struct FObjectNameKeyHash
{
    std::size_t operator()(const FObjectNameKey& Key) const noexcept
    {
        std::size_t Hash = FNameHash {}(Key.Name);
        Hash ^= static_cast<std::size_t>(Key.OuterHandle.Index)
            + 0x9e3779b9u + (Hash << 6u) + (Hash >> 2u);
        Hash ^= static_cast<std::size_t>(Key.OuterHandle.Serial)
            + 0x9e3779b9u + (Hash << 6u) + (Hash >> 2u);
        return Hash;
    }
};

struct FObjectHandleHash
{
    std::size_t operator()(FObjectHandle Handle) const noexcept
    {
        std::size_t Hash = static_cast<std::size_t>(Handle.Index);
        Hash ^= static_cast<std::size_t>(Handle.Serial)
            + 0x9e3779b9u + (Hash << 6u) + (Hash >> 2u);
        return Hash;
    }
};

using FChildHandleSet =
    std::unordered_set<FObjectHandle, FObjectHandleHash>;
using FObjectHierarchyIndex =
    std::unordered_map<FObjectHandle, FChildHandleSet, FObjectHandleHash>;

FObjectNameKey MakeObjectNameKey(const PObject* Outer, FName Name)
{
    return {Outer != nullptr ? Outer->GetHandle() : FObjectHandle {}, Name};
}

std::unordered_map<FObjectNameKey, FObjectHandle, FObjectNameKeyHash>&
GetObjectNameIndex()
{
    static std::unordered_map<FObjectNameKey, FObjectHandle, FObjectNameKeyHash>
        NameIndex;
    return NameIndex;
}

FObjectHierarchyIndex& GetObjectHierarchyIndex()
{
    static FObjectHierarchyIndex HierarchyIndex;
    return HierarchyIndex;
}

bool AddObjectHierarchyIndexEntry(const PObject* Object)
{
    if (Object == nullptr || Object->GetOuter() == nullptr) return true;
    return GetObjectHierarchyIndex()[Object->GetOuter()->GetHandle()]
        .insert(Object->GetHandle()).second;
}

void RemoveObjectHierarchyIndexEntry(const PObject* Object)
{
    if (Object == nullptr) return;
    FObjectHierarchyIndex& HierarchyIndex = GetObjectHierarchyIndex();
    if (const PObject* Outer = Object->GetOuter())
    {
        const auto Parent = HierarchyIndex.find(Outer->GetHandle());
        if (Parent != HierarchyIndex.end())
        {
            Parent->second.erase(Object->GetHandle());
            if (Parent->second.empty()) HierarchyIndex.erase(Parent);
        }
    }
    HierarchyIndex.erase(Object->GetHandle());
}

void RemoveObjectNameIndexEntry(const PObject* Object)
{
    if (Object == nullptr) return;
    auto& NameIndex = GetObjectNameIndex();
    const FObjectNameKey Key = MakeObjectNameKey(
        Object->GetOuter(), Object->GetName());
    const auto Found = NameIndex.find(Key);
    if (Found != NameIndex.end() && Found->second == Object->GetHandle())
        NameIndex.erase(Found);
}

std::vector<FObjectSlot>& GetObjectSlots()
{
    static std::vector<FObjectSlot> Slots;
    return Slots;
}

std::vector<uint32>& GetFreeObjectIndices()
{
    static std::vector<uint32> FreeIndices;
    return FreeIndices;
}

bool& IsDestroyingAllObjects()
{
    static bool bDestroyingAllObjects = false;
    return bDestroyingAllObjects;
}

bool& IsCollectingGarbage()
{
    static bool bCollectingGarbage = false;
    return bCollectingGarbage;
}

uint32 AllocateObjectSerial()
{
    static uint32 NextSerial = 1;
    const uint32 Serial = NextSerial++;
    if (NextSerial == 0)
    {
        NextSerial = 1;
    }
    return Serial;
}

bool HasChildObjects(const PObject* Parent)
{
    if (Parent == nullptr) return false;
    const auto& HierarchyIndex = GetObjectHierarchyIndex();
    const auto Found = HierarchyIndex.find(Parent->GetHandle());
    return Found != HierarchyIndex.end() && !Found->second.empty();
}

std::vector<FObjectHandle> GetChildObjectHandles(const PObject* Parent)
{
    if (Parent == nullptr) return {};
    const auto& HierarchyIndex = GetObjectHierarchyIndex();
    const auto Found = HierarchyIndex.find(Parent->GetHandle());
    if (Found == HierarchyIndex.end()) return {};
    return {Found->second.begin(), Found->second.end()};
}

void DestroyObjectTreeInternal(FObjectHandle RootHandle)
{
    PObject* Root = FObjectRegistry::ResolveObject(RootHandle);
    if (Root == nullptr)
    {
        return;
    }

    std::vector<FObjectHandle> Children = GetChildObjectHandles(Root);
    std::sort(
        Children.begin(),
        Children.end(),
        [](FObjectHandle Left, FObjectHandle Right)
        {
            return Left.Serial < Right.Serial;
        });

    for (FObjectHandle ChildHandle : Children)
    {
        DestroyObjectTreeInternal(ChildHandle);
    }

    FObjectRegistry::DestroyObject(FObjectRegistry::ResolveObject(RootHandle));
}
}

void FObjectRegistry::CallBeginDestroy(PObject* Object)
{
    if (Object == nullptr
        || Object->LifecycleState != PObject::ELifecycleState::Alive)
    {
        return;
    }

    Object->LifecycleState = PObject::ELifecycleState::BeginningDestroy;
    try
    {
        Object->BeginDestroy();
    }
    catch (const std::exception& Exception)
    {
        PICO_LOG(
            LogObject,
            Error,
            "BeginDestroy for '{}' threw an exception: {}",
            Object->GetPathName(),
            Exception.what());
    }
    catch (...)
    {
        PICO_LOG(LogObject, Error, "BeginDestroy for '{}' threw an unknown exception", Object->GetPathName());
    }
}

PObject* FObjectRegistry::AddObject(FObjectPtr Object, bool bDeferPostInitProperties)
{
    PICO_PROFILE_SCOPE("ObjectRegistry.Add");
    if (IsDestroyingAllObjects() || IsCollectingGarbage())
    {
        PICO_LOG(LogObject, Error, "Cannot register objects while the registry is shutting down");
        return nullptr;
    }

    if (Object == nullptr
        || Object->GetClass() == nullptr
        || !IsValidObjectName(Object->GetName()))
    {
        PICO_LOG(LogObject, Error, "Cannot register a null, untyped, or unnamed object");
        return nullptr;
    }

    if (Object->GetOuter() != nullptr
        && (ResolveObject(Object->GetOuter()->GetHandle()) != Object->GetOuter()
            || Object->GetOuter()->IsBeginningDestroy()))
    {
        PICO_LOG(
            LogObject,
            Error,
            "Outer for '{}' is not live or is being destroyed",
            Object->GetName().ToString());
        return nullptr;
    }

    if (FindObject(Object->GetOuter(), Object->GetName()) != nullptr)
    {
        PICO_LOG(LogObject, Error, "Object '{}' already exists in the requested outer", Object->GetName().ToString());
        return nullptr;
    }

    std::vector<FObjectSlot>& Slots = GetObjectSlots();
    std::vector<uint32>& FreeIndices = GetFreeObjectIndices();
    uint32 Index = 0;

    if (!FreeIndices.empty())
    {
        Index = FreeIndices.back();
        FreeIndices.pop_back();
    }
    else
    {
        if (Slots.size() >= FObjectHandle::InvalidIndex)
        {
            PICO_LOG(LogObject, Error, "Object registry has exhausted its handle index space");
            return nullptr;
        }

        Index = static_cast<uint32>(Slots.size());
        Slots.emplace_back();
    }

    FObjectSlot& Slot = Slots[Index];
    Slot.Serial = AllocateObjectSerial();
    Object->HandlePrivate = FObjectHandle { Index, Slot.Serial };

    PObject* RawObject = Object.get();
    Slot.Object = std::move(Object);
    const auto [IndexEntry, bInserted] = GetObjectNameIndex().emplace(
        MakeObjectNameKey(RawObject->GetOuter(), RawObject->GetName()),
        RawObject->GetHandle());
    (void)IndexEntry;
    if (!bInserted)
    {
        PICO_LOG(LogObject, Error,
            "Object name index rejected duplicate '{}'", RawObject->GetPathName());
        FObjectPtr RejectedObject = std::move(Slot.Object);
        RejectedObject->HandlePrivate = {};
        Slot.Serial = 0;
        GetFreeObjectIndices().push_back(Index);
        return nullptr;
    }

    if (!AddObjectHierarchyIndexEntry(RawObject))
    {
        PICO_LOG(LogObject, Error,
            "Object hierarchy index rejected duplicate '{}'", RawObject->GetPathName());
        GetObjectNameIndex().erase(IndexEntry);
        FObjectPtr RejectedObject = std::move(Slot.Object);
        RejectedObject->HandlePrivate = {};
        Slot.Serial = 0;
        GetFreeObjectIndices().push_back(Index);
        return nullptr;
    }

    if (!bDeferPostInitProperties)
    {
        PostInitObject(RawObject);
    }

    return RawObject;
}

void FObjectRegistry::PostInitObject(PObject* Object)
{
    if (Object == nullptr || ResolveObject(Object->GetHandle()) != Object)
    {
        return;
    }

    try
    {
        Object->PostInitProperties();
    }
    catch (...)
    {
        DestroyObjectTreeInternal(Object->GetHandle());
        throw;
    }
}

bool FObjectRegistry::DestroyObject(PObject* Object)
{
    PICO_PROFILE_SCOPE("ObjectRegistry.Destroy");
    if (IsCollectingGarbage()
        || Object == nullptr
        || Object->LifecycleState != PObject::ELifecycleState::Alive)
    {
        return false;
    }

    const FObjectHandle Handle = Object->GetHandle();
    if (ResolveObject(Handle) != Object)
    {
        return false;
    }

    if (HasChildObjects(Object))
    {
        PICO_LOG(LogObject, Warning, "Cannot destroy '{}' while it still has child objects", Object->GetPathName());
        return false;
    }

    CallBeginDestroy(Object);

    Object = ResolveObject(Handle);
    if (Object == nullptr
        || Object->LifecycleState != PObject::ELifecycleState::BeginningDestroy)
    {
        return false;
    }

    std::vector<FObjectSlot>& Slots = GetObjectSlots();
    FObjectSlot& Slot = Slots[Handle.Index];
    Object->LifecycleState = PObject::ELifecycleState::Destroying;
    RemoveObjectNameIndexEntry(Object);
    RemoveObjectHierarchyIndexEntry(Object);
    FObjectPtr OwnedObject = std::move(Slot.Object);
    OwnedObject->HandlePrivate = {};
    Slot.Serial = 0;
    OwnedObject.reset();
    GetFreeObjectIndices().push_back(Handle.Index);
    return true;
}

void FObjectRegistry::DestroyObjectTree(PObject* Root)
{
    if (Root != nullptr && ResolveObject(Root->GetHandle()) == Root)
    {
        DestroyObjectTreeInternal(Root->GetHandle());
    }
}

void FObjectRegistry::DestroyAllObjects()
{
    if (IsDestroyingAllObjects())
    {
        return;
    }

    IsDestroyingAllObjects() = true;
    const auto ResetDestroyingAll = MakeScopeExit(
        []()
        {
            IsDestroyingAllObjects() = false;
        });

    std::vector<FObjectHandle> LeafHandles;
    LeafHandles.reserve(GetObjectCount());
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        PObject* Object = Slot.Object.get();
        if (Object != nullptr && !HasChildObjects(Object))
            LeafHandles.push_back(Object->GetHandle());
    }
    while (!LeafHandles.empty())
    {
        const FObjectHandle Handle = LeafHandles.back();
        LeafHandles.pop_back();
        PObject* Object = ResolveObject(Handle);
        if (Object == nullptr || HasChildObjects(Object)) continue;
        const FObjectHandle OuterHandle = Object->GetOuter() != nullptr
            ? Object->GetOuter()->GetHandle() : FObjectHandle {};
        if (DestroyObject(Object) && OuterHandle.IsValid())
        {
            PObject* Outer = ResolveObject(OuterHandle);
            if (Outer != nullptr && !HasChildObjects(Outer))
                LeafHandles.push_back(OuterHandle);
        }
    }

    if (GetObjectCount() > 0)
    {
        PICO_LOG(LogObject, Error, "Object outer graph contains a cycle; forcing registry shutdown");
        for (FObjectSlot& Slot : GetObjectSlots())
        {
            if (Slot.Object == nullptr) continue;
            CallBeginDestroy(Slot.Object.get());
            if (Slot.Object != nullptr)
            {
                Slot.Object->LifecycleState = PObject::ELifecycleState::Destroying;
                Slot.Object->HandlePrivate = {};
                Slot.Object.reset();
                Slot.Serial = 0;
            }
        }
    }

    GetObjectSlots().clear();
    GetFreeObjectIndices().clear();
    GetObjectNameIndex().clear();
    GetObjectHierarchyIndex().clear();
}

PObject* FObjectRegistry::ResolveObject(FObjectHandle Handle)
{
    const std::vector<FObjectSlot>& Slots = GetObjectSlots();
    if (!Handle.IsValid() || Handle.Index >= Slots.size())
    {
        return nullptr;
    }

    const FObjectSlot& Slot = Slots[Handle.Index];
    return Slot.Object != nullptr && Slot.Serial == Handle.Serial ? Slot.Object.get() : nullptr;
}

PObject* FObjectRegistry::FindObject(PObject* Outer, FName Name)
{
    PICO_PROFILE_SCOPE("ObjectRegistry.Find");
    const auto& NameIndex = GetObjectNameIndex();
    const auto Found = NameIndex.find(MakeObjectNameKey(Outer, Name));
    return Found == NameIndex.end() ? nullptr : ResolveObject(Found->second);
}

bool FObjectRegistry::RenameObject(PObject* Object, FName NewName)
{
    PICO_PROFILE_SCOPE("ObjectRegistry.Rename");
    if (IsCollectingGarbage()
        || Object == nullptr
        || !IsValidObjectName(NewName)
        || ResolveObject(Object->GetHandle()) != Object
        || Object->IsBeginningDestroy()
        || HasAnyFlags(Object->GetFlags(), EObjectFlags::DefaultSubobject))
    {
        return false;
    }
    if (Object->GetName() == NewName)
    {
        return true;
    }
    if (FindObject(Object->GetOuter(), NewName) != nullptr)
    {
        return false;
    }

    auto& NameIndex = GetObjectNameIndex();
    const FObjectNameKey OldKey = MakeObjectNameKey(
        Object->GetOuter(), Object->GetName());
    const FObjectNameKey NewKey = MakeObjectNameKey(Object->GetOuter(), NewName);
    const auto [NewEntry, bInserted] = NameIndex.emplace(NewKey, Object->GetHandle());
    if (!bInserted) return false;
    const auto OldEntry = NameIndex.find(OldKey);
    if (OldEntry == NameIndex.end() || OldEntry->second != Object->GetHandle())
    {
        NameIndex.erase(NewEntry);
        return false;
    }
    NameIndex.erase(OldEntry);
    Object->NamePrivate = NewName;
    return true;
}

std::vector<PObject*> FObjectRegistry::GetObjects()
{
    std::vector<PObject*> Result;
    Result.reserve(GetObjectCount());
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        if (Slot.Object != nullptr)
        {
            Result.push_back(Slot.Object.get());
        }
    }
    return Result;
}

std::size_t FObjectRegistry::GetObjectCount()
{
    std::size_t Count = 0;
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        Count += Slot.Object != nullptr ? 1 : 0;
    }
    return Count;
}

bool FObjectRegistry::ValidateNameIndex(std::string* OutError)
{
    if (OutError) OutError->clear();
    const auto Fail = [OutError](std::string Error)
    {
        if (OutError) *OutError = std::move(Error);
        return false;
    };
    const auto& Slots = GetObjectSlots();
    const auto& NameIndex = GetObjectNameIndex();
    std::size_t LiveCount = 0;
    for (const FObjectSlot& Slot : Slots)
    {
        const PObject* Object = Slot.Object.get();
        if (Object == nullptr) continue;
        ++LiveCount;
        const auto Found = NameIndex.find(MakeObjectNameKey(
            Object->GetOuter(), Object->GetName()));
        if (Found == NameIndex.end() || Found->second != Object->GetHandle())
            return Fail("Live object is missing or mismatched in the name index: "
                + Object->GetPathName());
    }
    if (NameIndex.size() != LiveCount)
        return Fail("Object name index entry count does not match live object count");
    for (const auto& [Key, Handle] : NameIndex)
    {
        const PObject* Object = ResolveObject(Handle);
        if (Object == nullptr
            || MakeObjectNameKey(Object->GetOuter(), Object->GetName()) != Key)
            return Fail("Object name index contains a stale entry");
    }
    return true;
}

bool FObjectRegistry::ValidateHierarchyIndex(std::string* OutError)
{
    if (OutError) OutError->clear();
    const auto Fail = [OutError](std::string Error)
    {
        if (OutError) *OutError = std::move(Error);
        return false;
    };
    const auto& HierarchyIndex = GetObjectHierarchyIndex();
    std::size_t ExpectedRelations = 0;
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        const PObject* Object = Slot.Object.get();
        if (Object == nullptr || Object->GetOuter() == nullptr) continue;
        ++ExpectedRelations;
        const auto Parent = HierarchyIndex.find(Object->GetOuter()->GetHandle());
        if (Parent == HierarchyIndex.end()
            || !Parent->second.contains(Object->GetHandle()))
            return Fail("Live object is missing from the hierarchy index: "
                + Object->GetPathName());
    }

    std::size_t IndexedRelations = 0;
    for (const auto& [ParentHandle, Children] : HierarchyIndex)
    {
        const PObject* Parent = ResolveObject(ParentHandle);
        if (Parent == nullptr || Children.empty())
            return Fail("Object hierarchy index contains a stale parent entry");
        IndexedRelations += Children.size();
        for (FObjectHandle ChildHandle : Children)
        {
            const PObject* Child = ResolveObject(ChildHandle);
            if (Child == nullptr || Child->GetOuter() != Parent)
                return Fail("Object hierarchy index contains a stale child entry");
        }
    }
    if (IndexedRelations != ExpectedRelations)
        return Fail("Object hierarchy index relation count does not match live objects");
    return true;
}

FObjectHierarchyIndexStats FObjectRegistry::GetHierarchyIndexStats()
{
    const FObjectHierarchyIndex& HierarchyIndex = GetObjectHierarchyIndex();
    FObjectHierarchyIndexStats Stats;
    Stats.ParentEntryCount = HierarchyIndex.size();
    Stats.EstimatedStorageBytes = HierarchyIndex.bucket_count() * sizeof(void*)
        + HierarchyIndex.size()
            * (sizeof(FObjectHandle) + sizeof(FChildHandleSet));
    for (const auto& [Parent, Children] : HierarchyIndex)
    {
        (void)Parent;
        Stats.ChildRelationCount += Children.size();
        Stats.EstimatedStorageBytes += Children.bucket_count() * sizeof(void*)
            + Children.size() * sizeof(FObjectHandle);
    }
    return Stats;
}

void FObjectRegistry::PublishMemoryStatistics()
{
    FMemoryTracker& Tracker = FMemoryTracker::Get();
    if (!Tracker.IsEnabled()) return;

    const std::vector<FObjectSlot>& Slots = GetObjectSlots();
    Tracker.Report(EMemoryTag::ObjectSlots,
        Slots.size() * sizeof(FObjectSlot),
        Slots.capacity() * sizeof(FObjectSlot), Slots.size());

    const auto& NameIndex = GetObjectNameIndex();
    const std::size_t NamePayloadBytes = NameIndex.size()
        * (sizeof(FObjectNameKey) + sizeof(FObjectHandle));
    const std::size_t NameReservedBytes = NamePayloadBytes
        + NameIndex.bucket_count() * sizeof(void*);
    Tracker.Report(EMemoryTag::ObjectNameIndex,
        NamePayloadBytes, NameReservedBytes, NameIndex.size());

    const FObjectHierarchyIndex& HierarchyIndex = GetObjectHierarchyIndex();
    const FObjectHierarchyIndexStats HierarchyStats = GetHierarchyIndexStats();
    std::size_t HierarchyPayloadBytes = HierarchyIndex.size()
        * (sizeof(FObjectHandle) + sizeof(FChildHandleSet));
    for (const auto& [Parent, Children] : HierarchyIndex)
    {
        (void)Parent;
        HierarchyPayloadBytes += Children.size() * sizeof(FObjectHandle);
    }
    Tracker.Report(EMemoryTag::ObjectHierarchyIndex,
        HierarchyPayloadBytes, HierarchyStats.EstimatedStorageBytes,
        HierarchyStats.ChildRelationCount);
}

bool FObjectRegistry::AddToRoot(PObject* Object)
{
    if (IsCollectingGarbage()
        || Object == nullptr
        || ResolveObject(Object->GetHandle()) != Object
        || Object->IsBeginningDestroy())
    {
        return false;
    }
    Object->FlagsPrivate = static_cast<EObjectFlags>(
        static_cast<uint32>(Object->FlagsPrivate)
        | static_cast<uint32>(EObjectFlags::RootSet));
    return true;
}

bool FObjectRegistry::RemoveFromRoot(PObject* Object)
{
    if (IsCollectingGarbage()
        || Object == nullptr
        || ResolveObject(Object->GetHandle()) != Object
        || Object->IsBeginningDestroy())
    {
        return false;
    }
    Object->FlagsPrivate = static_cast<EObjectFlags>(
        static_cast<uint32>(Object->FlagsPrivate)
        & ~static_cast<uint32>(EObjectFlags::RootSet));
    return true;
}

bool FObjectRegistry::IsRooted(const PObject* Object)
{
    return Object != nullptr
        && ResolveObject(Object->GetHandle()) == Object
        && HasAnyFlags(Object->GetFlags(), EObjectFlags::RootSet);
}

bool FObjectRegistry::IsGarbageCollecting()
{
    return IsCollectingGarbage();
}

FGarbageCollectionResult FObjectRegistry::CollectGarbage()
{
    FGarbageCollectionResult Result;
    Result.ObjectCountBefore = GetObjectCount();
    Result.ObjectCountAfter = Result.ObjectCountBefore;
    if (IsDestroyingAllObjects() || IsCollectingGarbage())
    {
        return Result;
    }

    IsCollectingGarbage() = true;
    const auto ResetCollecting = MakeScopeExit([]() { IsCollectingGarbage() = false; });
    FMemoryTracker& MemoryTracker = FMemoryTracker::Get();
    const auto ResetScratchMemory = MakeScopeExit([&MemoryTracker]()
    {
        MemoryTracker.Report(EMemoryTag::GCScratch, 0, 0, 0);
    });
    FProfileScopeToken MarkScope = FProfiler::Get().BeginScope("GC.Mark");
    std::vector<FObjectSlot>& Slots = GetObjectSlots();
    std::vector<bool> Marked(Slots.size(), false);
    std::vector<FObjectHandle> WorkStack;
    std::size_t WorkStackPeakSize = 0;
    std::size_t ReferencePeakBytes = 0;

    for (const FObjectSlot& Slot : Slots)
    {
        if (Slot.Object != nullptr
            && HasAnyFlags(Slot.Object->GetFlags(), EObjectFlags::RootSet))
        {
            WorkStack.push_back(Slot.Object->GetHandle());
            WorkStackPeakSize = std::max(WorkStackPeakSize, WorkStack.size());
            ++Result.RootCount;
        }
    }

    while (!WorkStack.empty())
    {
        const FObjectHandle Handle = WorkStack.back();
        WorkStack.pop_back();
        PObject* Object = ResolveObject(Handle);
        if (Object == nullptr || Marked[Handle.Index])
        {
            continue;
        }

        Marked[Handle.Index] = true;
        ++Result.ReachableObjectCount;

        FReferenceCollector Collector;
        Object->AddReferencedObjects(Collector);
        ReferencePeakBytes = std::max(
            ReferencePeakBytes, Collector.GetReservedBytes());
        for (const FObjectHandle Reference : Collector.GetReferences())
        {
            WorkStack.push_back(Reference);
        }

        for (const PClass* Class = Object->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
        {
            for (const PProperty& Property : Class->GetProperties())
            {
                if (Property.GetObjectReferenceKind() == EObjectReferenceKind::Strong)
                {
                    if (PObject* Referenced = Property.GetReferencedObject(Object))
                    {
                        WorkStack.push_back(Referenced->GetHandle());
                    }
                }
            }
        }
        WorkStackPeakSize = std::max(WorkStackPeakSize, WorkStack.size());
    }

    const std::size_t MarkedCurrentBytes = (Marked.size() + 7) / 8;
    const std::size_t MarkedReservedBytes = (Marked.capacity() + 7) / 8;
    const std::size_t MarkCurrentBytes = MarkedCurrentBytes
        + WorkStackPeakSize * sizeof(FObjectHandle) + ReferencePeakBytes;
    const std::size_t MarkReservedBytes = MarkedReservedBytes
        + WorkStack.capacity() * sizeof(FObjectHandle) + ReferencePeakBytes;
    MemoryTracker.Report(EMemoryTag::GCScratch,
        MarkCurrentBytes, MarkReservedBytes,
        Marked.size() + WorkStackPeakSize);

    FProfiler::Get().EndScope(MarkScope);
    PICO_PROFILE_SCOPE("GC.Sweep");
    struct FUnreachableObject
    {
        FObjectHandle Handle;
        std::size_t OuterDepth = 0;
    };
    std::vector<FUnreachableObject> Unreachable;
    for (std::size_t Index = 0; Index < Slots.size(); ++Index)
    {
        PObject* Object = Slots[Index].Object.get();
        if (Object == nullptr || Marked[Index])
        {
            continue;
        }
        std::size_t Depth = 0;
        for (const PObject* Outer = Object->GetOuter(); Outer != nullptr; Outer = Outer->GetOuter())
        {
            ++Depth;
        }
        Unreachable.push_back({Object->GetHandle(), Depth});
    }
    std::sort(
        Unreachable.begin(),
        Unreachable.end(),
        [](const FUnreachableObject& Left, const FUnreachableObject& Right)
        {
            return Left.OuterDepth > Right.OuterDepth;
        });

    const std::size_t SweepCurrentBytes = MarkedCurrentBytes
        + Unreachable.size() * sizeof(FUnreachableObject);
    const std::size_t SweepReservedBytes = MarkedReservedBytes
        + WorkStack.capacity() * sizeof(FObjectHandle)
        + Unreachable.capacity() * sizeof(FUnreachableObject)
        + ReferencePeakBytes;
    MemoryTracker.Report(EMemoryTag::GCScratch,
        SweepCurrentBytes, SweepReservedBytes,
        Marked.size() + Unreachable.size());

    for (const FUnreachableObject& Entry : Unreachable)
    {
        CallBeginDestroy(ResolveObject(Entry.Handle));
    }
    for (const FUnreachableObject& Entry : Unreachable)
    {
        PObject* Object = ResolveObject(Entry.Handle);
        if (Object == nullptr)
        {
            continue;
        }
        FObjectSlot& Slot = Slots[Entry.Handle.Index];
        Object->LifecycleState = PObject::ELifecycleState::Destroying;
        RemoveObjectNameIndexEntry(Object);
        RemoveObjectHierarchyIndexEntry(Object);
        FObjectPtr OwnedObject = std::move(Slot.Object);
        OwnedObject->HandlePrivate = {};
        Slot.Serial = 0;
        OwnedObject.reset();
        GetFreeObjectIndices().push_back(Entry.Handle.Index);
        ++Result.CollectedObjectCount;
    }

    Result.ObjectCountAfter = GetObjectCount();
    Result.bSucceeded = true;
    return Result;
}
}
