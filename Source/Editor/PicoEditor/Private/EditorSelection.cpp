#include "Pico/Editor/EditorSelection.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

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
    return ResolveObject(Handle);
}

FObjectHandle FEditorSelection::GetHandle() const
{
    return Handle;
}

std::string FEditorSelection::GetObjectPath() const
{
    PObject* Object = Resolve();
    return Object != nullptr ? Object->GetPathName() : std::string {};
}

bool FEditorSelection::IsValid() const
{
    return Handle.IsValid();
}

bool FEditorSelection::Set(PObject* Object)
{
    const FObjectHandle NewHandle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    if (NewHandle == Handle)
    {
        return false;
    }
    Handle = NewHandle;
    return true;
}

void FEditorSelection::Clear()
{
    Handle = {};
}

bool FEditorSelection::Validate()
{
    if (!Handle.IsValid() || Resolve() != nullptr)
    {
        return true;
    }
    Clear();
    return false;
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
}
