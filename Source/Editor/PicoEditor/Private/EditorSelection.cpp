#include "Pico/Editor/EditorSelection.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>

namespace Pico
{
PObject* FindEditorWorldObjectByPath(PWorld* World, std::string_view Path)
{
    if (World == nullptr || Path.empty())
    {
        return nullptr;
    }
    if (World->GetPathName() == Path)
    {
        return World;
    }
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        if (Level->GetPathName() == Path)
        {
            return Level;
        }
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr)
            {
                continue;
            }
            if (Actor->GetPathName() == Path)
            {
                return Actor;
            }
            for (PActorComponent* Component : Actor->GetComponents())
            {
                if (Component != nullptr && Component->GetPathName() == Path)
                {
                    return Component;
                }
            }
        }
    }
    return nullptr;
}

PObject* FEditorSelection::Resolve() const
{
    return ResolveObject(PrimaryHandle);
}

std::vector<PObject*> FEditorSelection::ResolveAll() const
{
    std::vector<PObject*> Objects;
    Objects.reserve(Handles.size());
    for (FObjectHandle Handle : Handles)
    {
        if (PObject* Object = ResolveObject(Handle))
        {
            Objects.push_back(Object);
        }
    }
    return Objects;
}

FObjectHandle FEditorSelection::GetHandle() const
{
    return PrimaryHandle;
}

const std::vector<FObjectHandle>& FEditorSelection::GetHandles() const
{
    return Handles;
}

std::string FEditorSelection::GetObjectPath() const
{
    PObject* Object = Resolve();
    return Object != nullptr ? Object->GetPathName() : std::string {};
}

std::vector<std::string> FEditorSelection::GetObjectPaths() const
{
    std::vector<std::string> Paths;
    Paths.reserve(Handles.size());
    for (PObject* Object : ResolveAll())
    {
        Paths.push_back(Object->GetPathName());
    }
    return Paths;
}

std::size_t FEditorSelection::Num() const
{
    return Handles.size();
}

std::uint64_t FEditorSelection::GetRevision() const
{
    return Revision;
}

bool FEditorSelection::IsValid() const
{
    return PrimaryHandle.IsValid() && !Handles.empty();
}

bool FEditorSelection::Contains(const PObject* Object) const
{
    return Object != nullptr && Contains(Object->GetHandle());
}

bool FEditorSelection::Contains(FObjectHandle ObjectHandle) const
{
    return ObjectHandle.IsValid()
        && std::find(Handles.begin(), Handles.end(), ObjectHandle) != Handles.end();
}

bool FEditorSelection::Set(PObject* Object)
{
    const FObjectHandle NewHandle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    if (Handles.size() == (NewHandle.IsValid() ? 1u : 0u)
        && PrimaryHandle == NewHandle)
    {
        return false;
    }
    Handles.clear();
    if (NewHandle.IsValid())
    {
        Handles.push_back(NewHandle);
    }
    PrimaryHandle = NewHandle;
    RangeAnchorHandle = NewHandle;
    ++Revision;
    return true;
}

bool FEditorSelection::Add(PObject* Object)
{
    const FObjectHandle NewHandle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    if (!NewHandle.IsValid())
    {
        return false;
    }
    const bool bAlreadySelected = Contains(NewHandle);
    const bool bPrimaryChanged = PrimaryHandle != NewHandle;
    if (!bAlreadySelected)
    {
        Handles.push_back(NewHandle);
    }
    PrimaryHandle = NewHandle;
    RangeAnchorHandle = NewHandle;
    if (!bAlreadySelected || bPrimaryChanged) ++Revision;
    return !bAlreadySelected || bPrimaryChanged;
}

bool FEditorSelection::Remove(PObject* Object)
{
    const FObjectHandle Handle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    const auto Found = std::find(Handles.begin(), Handles.end(), Handle);
    if (Found == Handles.end())
    {
        return false;
    }
    Handles.erase(Found);
    if (PrimaryHandle == Handle)
    {
        PrimaryHandle = Handles.empty() ? FObjectHandle {} : Handles.back();
    }
    if (RangeAnchorHandle == Handle)
    {
        RangeAnchorHandle = PrimaryHandle;
    }
    ++Revision;
    return true;
}

bool FEditorSelection::Toggle(PObject* Object)
{
    if (Contains(Object))
    {
        return Remove(Object);
    }
    return Add(Object);
}

bool FEditorSelection::SetRange(
    const std::vector<PObject*>& OrderedObjects,
    PObject* Object,
    bool bAppend)
{
    if (Object == nullptr)
    {
        return false;
    }
    const FObjectHandle TargetHandle = Object->GetHandle();
    const auto Target = std::find(OrderedObjects.begin(), OrderedObjects.end(), Object);
    PObject* AnchorObject = ResolveObject(RangeAnchorHandle);
    const auto Anchor = std::find(OrderedObjects.begin(), OrderedObjects.end(), AnchorObject);
    if (Target == OrderedObjects.end() || Anchor == OrderedObjects.end())
    {
        return bAppend ? Add(Object) : Set(Object);
    }

    const auto First = std::min(Target, Anchor);
    const auto Last = std::max(Target, Anchor);
    std::vector<FObjectHandle> NewHandles = bAppend
        ? Handles : std::vector<FObjectHandle> {};
    for (auto It = First; It <= Last; ++It)
    {
        PObject* RangeObject = *It;
        if (RangeObject == nullptr)
        {
            continue;
        }
        const FObjectHandle Handle = RangeObject->GetHandle();
        if (std::find(NewHandles.begin(), NewHandles.end(), Handle) == NewHandles.end())
        {
            NewHandles.push_back(Handle);
        }
    }
    const bool bChanged = NewHandles != Handles || PrimaryHandle != TargetHandle;
    Handles = std::move(NewHandles);
    PrimaryHandle = TargetHandle;
    if (bChanged) ++Revision;
    return bChanged;
}

void FEditorSelection::Clear()
{
    const bool bChanged = !Handles.empty() || PrimaryHandle.IsValid()
        || RangeAnchorHandle.IsValid();
    Handles.clear();
    PrimaryHandle = {};
    RangeAnchorHandle = {};
    if (bChanged) ++Revision;
}

bool FEditorSelection::Validate()
{
    const std::size_t PreviousSize = Handles.size();
    const FObjectHandle PreviousPrimary = PrimaryHandle;
    const FObjectHandle PreviousAnchor = RangeAnchorHandle;
    std::erase_if(
        Handles,
        [](FObjectHandle Handle) { return ResolveObject(Handle) == nullptr; });
    if (!Contains(PrimaryHandle))
    {
        PrimaryHandle = Handles.empty() ? FObjectHandle {} : Handles.back();
    }
    if (!Contains(RangeAnchorHandle))
    {
        RangeAnchorHandle = PrimaryHandle;
    }
    if (PreviousSize != Handles.size() || PreviousPrimary != PrimaryHandle
        || PreviousAnchor != RangeAnchorHandle)
        ++Revision;
    return PreviousSize == Handles.size();
}

PObject* FEditorSelection::Restore(PWorld* World, std::string_view ObjectPath)
{
    PObject* Object = FindEditorWorldObjectByPath(World, ObjectPath);
    if (Object == nullptr && !ObjectPath.empty())
    {
        Object = World;
    }
    Set(Object);
    return Object;
}

PObject* FEditorSelection::Restore(
    PWorld* World,
    const std::vector<std::string>& ObjectPaths,
    std::string_view PrimaryObjectPath)
{
    Clear();
    for (const std::string& Path : ObjectPaths)
    {
        if (PObject* Object = FindEditorWorldObjectByPath(World, Path))
        {
            Add(Object);
        }
    }

    PObject* PrimaryObject = FindEditorWorldObjectByPath(World, PrimaryObjectPath);
    if (PrimaryObject != nullptr)
    {
        if (!Contains(PrimaryObject))
        {
            Handles.push_back(PrimaryObject->GetHandle());
        }
        PrimaryHandle = PrimaryObject->GetHandle();
        RangeAnchorHandle = PrimaryHandle;
    }
    else if (Handles.empty() && World != nullptr && !PrimaryObjectPath.empty())
    {
        Set(World);
    }
    return Resolve();
}
}
