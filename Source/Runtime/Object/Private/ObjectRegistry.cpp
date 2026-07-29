#include "Pico/Object/ObjectRegistry.h"

#include "Pico/Core/Log.h"
#include "Pico/Object/Object.h"

#include <exception>
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

void DestroyObjectTreeInternal(PObject* Root)
{
    std::vector<PObject*> Children;
    for (const FObjectSlot& Slot : GetObjectSlots())
    {
        if (Slot.Object != nullptr && Slot.Object->GetOuter() == Root)
        {
            Children.push_back(Slot.Object.get());
        }
    }

    for (PObject* Child : Children)
    {
        DestroyObjectTreeInternal(Child);
    }

    FObjectRegistry::DestroyObject(Root);
}
}

void FObjectRegistry::CallBeginDestroy(PObject* Object)
{
    if (Object == nullptr || Object->bBeginningDestroy)
    {
        return;
    }

    Object->bBeginningDestroy = true;
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

PObject* FObjectRegistry::AddObject(FObjectPtr Object)
{
    if (Object == nullptr || Object->GetClass() == nullptr || Object->GetName().IsNone())
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

    try
    {
        RawObject->PostInitProperties();
    }
    catch (...)
    {
        DestroyObjectTreeInternal(RawObject);
        throw;
    }

    return RawObject;
}

bool FObjectRegistry::DestroyObject(PObject* Object)
{
    if (Object == nullptr)
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

    std::vector<FObjectSlot>& Slots = GetObjectSlots();
    FObjectSlot& Slot = Slots[Handle.Index];
    CallBeginDestroy(Object);
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
        DestroyObjectTreeInternal(Root);
    }
}

void FObjectRegistry::DestroyAllObjects()
{
    while (GetObjectCount() > 0)
    {
        bool bDestroyedObject = false;
        for (FObjectSlot& Slot : GetObjectSlots())
        {
            PObject* Object = Slot.Object.get();
            if (Object != nullptr && !HasChildObjects(Object))
            {
                DestroyObject(Object);
                bDestroyedObject = true;
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
                    Slot.Object->HandlePrivate = {};
                    Slot.Object.reset();
                    Slot.Serial = 0;
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
    if (Object == nullptr
        || NewName.IsNone()
        || ResolveObject(Object->GetHandle()) != Object
        || Object->IsBeginningDestroy())
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
}
