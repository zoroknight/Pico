#include "Pico/Object/ObjectRegistry.h"

#include "Pico/Core/Log.h"
#include "Pico/Core/ScopeExit.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectName.h"
#include "Pico/Object/ReferenceCollector.h"

#include <algorithm>
#include <exception>
#include <numeric>
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
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        if (Slot.Object != nullptr && Slot.Object->GetOuter() == Parent)
        {
            return true;
        }
    }
    return false;
}

void DestroyObjectTreeInternal(FObjectHandle RootHandle)
{
    PObject* Root = FObjectRegistry::ResolveObject(RootHandle);
    if (Root == nullptr)
    {
        return;
    }

    std::vector<FObjectHandle> Children;
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        if (Slot.Object != nullptr && Slot.Object->GetOuter() == Root)
        {
            Children.push_back(Slot.Object->GetHandle());
        }
    }
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

    while (GetObjectCount() > 0)
    {
        bool bDestroyedObject = false;
        std::vector<FObjectHandle> LeafHandles;
        for (const FObjectSlot& Slot : GetObjectSlots())
        {
            PObject* Object = Slot.Object.get();
            if (Object != nullptr && !HasChildObjects(Object))
            {
                LeafHandles.push_back(Object->GetHandle());
            }
        }
        for (FObjectHandle Handle : LeafHandles)
        {
            if (PObject* Object = ResolveObject(Handle))
            {
                bDestroyedObject = DestroyObject(Object) || bDestroyedObject;
            }
        }

        if (!bDestroyedObject)
        {
            PICO_LOG(LogObject, Error, "Object outer graph contains a cycle; forcing registry shutdown");
            for (FObjectSlot& Slot : GetObjectSlots())
            {
                if (Slot.Object != nullptr)
                {
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
        }
    }

    GetObjectSlots().clear();
    GetFreeObjectIndices().clear();
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
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        if (Slot.Object != nullptr && Slot.Object->GetOuter() == Outer && Slot.Object->GetName() == Name)
        {
            return Slot.Object.get();
        }
    }
    return nullptr;
}

bool FObjectRegistry::RenameObject(PObject* Object, FName NewName)
{
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
    std::vector<FObjectSlot>& Slots = GetObjectSlots();
    std::vector<bool> Marked(Slots.size(), false);
    std::vector<FObjectHandle> WorkStack;

    for (const FObjectSlot& Slot : Slots)
    {
        if (Slot.Object != nullptr
            && HasAnyFlags(Slot.Object->GetFlags(), EObjectFlags::RootSet))
        {
            WorkStack.push_back(Slot.Object->GetHandle());
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
    }

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
