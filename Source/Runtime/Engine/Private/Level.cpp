#include "Pico/Engine/Level.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PLevel)

PLevel::PLevel(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

PWorld* PLevel::GetWorld() const
{
    PObject* Outer = GetOuter();
    return Outer != nullptr && Outer->IsA(PWorld::StaticClass())
        ? static_cast<PWorld*>(Outer)
        : nullptr;
}

std::vector<PActor*> PLevel::GetActors() const
{
    std::vector<PActor*> Actors;
    Actors.reserve(ActorHandles.size());
    for (const FObjectHandle Handle : ActorHandles)
    {
        if (PActor* Actor = ResolveActor(Handle))
        {
            Actors.push_back(Actor);
        }
    }
    return Actors;
}

void PLevel::AddActor(PActor* Actor)
{
    if (Actor != nullptr && !OwnsActor(Actor))
    {
        ActorHandles.push_back(Actor->GetHandle());
    }
}

bool PLevel::RemoveActor(PActor* Actor)
{
    const auto Existing = std::find_if(
        ActorHandles.begin(),
        ActorHandles.end(),
        [this, Actor](FObjectHandle Handle)
        {
            return ResolveActor(Handle) == Actor;
        });

    if (Existing == ActorHandles.end())
    {
        return false;
    }

    ActorHandles.erase(Existing);
    return true;
}

PActor* PLevel::ResolveActor(FObjectHandle Handle) const
{
    PObject* Object = ResolveObject(Handle);
    return Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object)
        : nullptr;
}

bool PLevel::OwnsActor(const PActor* Actor) const
{
    if (Actor == nullptr)
    {
        return false;
    }

    return std::any_of(
        ActorHandles.begin(),
        ActorHandles.end(),
        [this, Actor](FObjectHandle Handle)
        {
            return ResolveActor(Handle) == Actor;
        });
}
}
