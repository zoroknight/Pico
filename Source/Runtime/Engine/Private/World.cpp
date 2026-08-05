#include "Pico/Engine/World.h"

#include "Pico/Core/ScopeExit.h"

#include "Pico/Core/Log.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/Level.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PWorld)

PWorld::PWorld(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

bool PWorld::Initialize()
{
    if (State != EWorldState::Uninitialized)
    {
        PICO_LOG(LogEngine, Error, "World '{}' cannot be initialized from its current state", GetPathName());
        return false;
    }

    PLevel* PersistentLevel = NewObject<PLevel>(this, "PersistentLevel");
    if (PersistentLevel == nullptr)
    {
        PICO_LOG(LogEngine, Error, "World '{}' failed to create its persistent level", GetPathName());
        return false;
    }

    PersistentLevelHandle = PersistentLevel->GetHandle();
    CurrentLevelHandle = PersistentLevelHandle;
    LevelHandles.push_back(PersistentLevelHandle);
    State = EWorldState::Initialized;

    PICO_LOG(LogEngine, Info, "World '{}' initialized with persistent level '{}'",
        GetPathName(), PersistentLevel->GetPathName());
    return true;
}

void PWorld::Tick(float DeltaSeconds)
{
    if (State != EWorldState::Initialized)
    {
        PICO_LOG(LogEngine, Warning, "World '{}' ignored Tick outside the initialized state", GetPathName());
        return;
    }

    if (!std::isfinite(DeltaSeconds) || DeltaSeconds < 0.0f)
    {
        PICO_LOG(LogEngine, Warning, "World '{}' ignored invalid delta time {}", GetPathName(), DeltaSeconds);
        return;
    }

    ++TickCount;
    TimeSeconds += static_cast<double>(DeltaSeconds);

    if (!bHasBegunPlay)
    {
        BeginPlay();
    }

    bTickingActors = true;
    auto ResetTickingActors = MakeScopeExit(
        [this]()
        {
            bTickingActors = false;
        });
    const std::vector<PLevel*> Levels = GetLevels();
    for (PLevel* Level : Levels)
    {
        if (Level == nullptr)
        {
            continue;
        }

        const std::vector<PActor*> Actors = Level->GetActors();
        for (PActor* Actor : Actors)
        {
            if (Actor != nullptr && OwnsActor(Actor))
            {
                Actor->DispatchTick(DeltaSeconds);
            }
        }
    }
    ResetTickingActors.Release();
    bTickingActors = false;
    ProcessPendingDestroyActors();
}

void PWorld::TearDown()
{
    if (State == EWorldState::TearingDown || State == EWorldState::TornDown)
    {
        return;
    }

    State = EWorldState::TearingDown;

    const std::vector<PLevel*> Levels = GetLevels();
    for (PLevel* Level : Levels)
    {
        if (Level == nullptr)
        {
            continue;
        }

        const std::vector<PActor*> Actors = Level->GetActors();
        for (PActor* Actor : Actors)
        {
            if (Actor != nullptr)
            {
                Actor->DispatchEndPlay();
            }
        }
    }

    for (const FObjectHandle Handle : LevelHandles)
    {
        if (PLevel* Level = ResolveLevel(Handle))
        {
            DestroyObjectTree(Level);
        }
    }

    LevelHandles.clear();
    PersistentLevelHandle = {};
    CurrentLevelHandle = {};
    PendingDestroyActorHandles.clear();
    State = EWorldState::TornDown;
    PICO_LOG(LogEngine, Info, "World '{}' torn down after {} ticks", GetPathName(), TickCount);
}

PLevel* PWorld::CreateLevel(FName Name)
{
    if (State != EWorldState::Initialized)
    {
        PICO_LOG(LogEngine, Error, "World '{}' cannot create a level outside the initialized state", GetPathName());
        return nullptr;
    }

    PLevel* Level = NewObject<PLevel>(this, Name);
    if (Level != nullptr)
    {
        LevelHandles.push_back(Level->GetHandle());
    }
    return Level;
}

PLevel* PWorld::CreateLevel(std::string_view Name)
{
    return CreateLevel(FName(Name));
}

bool PWorld::RemoveLevel(PLevel* Level)
{
    if (State != EWorldState::Initialized || Level == nullptr)
    {
        return false;
    }

    const auto Existing = std::find_if(
        LevelHandles.begin(),
        LevelHandles.end(),
        [this, Level](FObjectHandle Handle)
        {
            return ResolveLevel(Handle) == Level;
        });

    if (Existing == LevelHandles.end() || *Existing == PersistentLevelHandle)
    {
        return false;
    }

    const FObjectHandle RemovedHandle = *Existing;
    LevelHandles.erase(Existing);
    if (CurrentLevelHandle == RemovedHandle)
    {
        CurrentLevelHandle = PersistentLevelHandle;
    }

    if (PLevel* LiveLevel = ResolveLevel(RemovedHandle))
    {
        DestroyObjectTree(LiveLevel);
    }
    return true;
}

bool PWorld::SetCurrentLevel(PLevel* Level)
{
    if (State != EWorldState::Initialized || !OwnsLevel(Level))
    {
        return false;
    }

    CurrentLevelHandle = Level->GetHandle();
    return true;
}

PActor* PWorld::SpawnActor(const PClass* ActorClass, FName Name, PLevel* Level)
{
    if (State != EWorldState::Initialized)
    {
        PICO_LOG(LogEngine, Error, "World '{}' cannot spawn actors outside the initialized state", GetPathName());
        return nullptr;
    }

    if (ActorClass == nullptr || !ActorClass->IsChildOf(PActor::StaticClass()))
    {
        PICO_LOG(LogEngine, Error, "World '{}' requires a PActor-derived class to spawn an actor", GetPathName());
        return nullptr;
    }

    PLevel* TargetLevel = Level != nullptr ? Level : GetCurrentLevel();
    if (!OwnsLevel(TargetLevel))
    {
        PICO_LOG(LogEngine, Error, "World '{}' cannot spawn an actor into an unowned level", GetPathName());
        return nullptr;
    }

    PObject* Object = NewObject(ActorClass, TargetLevel, Name);
    if (Object == nullptr)
    {
        return nullptr;
    }

    PActor* Actor = static_cast<PActor*>(Object);
    TargetLevel->AddActor(Actor);
    if (bHasBegunPlay)
    {
        Actor->DispatchBeginPlay();
    }
    return Actor;
}

PActor* PWorld::SpawnActor(const PClass* ActorClass, std::string_view Name, PLevel* Level)
{
    return SpawnActor(ActorClass, FName(Name), Level);
}

bool PWorld::DestroyActor(PActor* Actor)
{
    if (!OwnsActor(Actor))
    {
        return false;
    }

    if (Actor->IsPendingDestroy())
    {
        return true;
    }

    Actor->MarkPendingDestroy();
    Actor->DispatchEndPlay();

    if (PLevel* Level = Actor->GetLevel())
    {
        Level->RemoveActor(Actor);
    }

    if (bTickingActors)
    {
        PendingDestroyActorHandles.push_back(Actor->GetHandle());
    }
    else
    {
        DestroyActorNow(Actor);
    }
    return true;
}

PLevel* PWorld::GetPersistentLevel() const
{
    return ResolveLevel(PersistentLevelHandle);
}

PLevel* PWorld::GetCurrentLevel() const
{
    if (PLevel* CurrentLevel = ResolveLevel(CurrentLevelHandle))
    {
        return CurrentLevel;
    }
    return GetPersistentLevel();
}

std::vector<PLevel*> PWorld::GetLevels() const
{
    std::vector<PLevel*> Levels;
    Levels.reserve(LevelHandles.size());
    for (const FObjectHandle Handle : LevelHandles)
    {
        if (PLevel* Level = ResolveLevel(Handle))
        {
            Levels.push_back(Level);
        }
    }
    return Levels;
}

EWorldState PWorld::GetState() const
{
    return State;
}

uint64 PWorld::GetTickCount() const
{
    return TickCount;
}

double PWorld::GetTimeSeconds() const
{
    return TimeSeconds;
}

void PWorld::BeginDestroy()
{
    TearDown();
    PObject::BeginDestroy();
}

PLevel* PWorld::ResolveLevel(FObjectHandle Handle) const
{
    PObject* Object = ResolveObject(Handle);
    return Object != nullptr && Object->IsA(PLevel::StaticClass())
        ? static_cast<PLevel*>(Object)
        : nullptr;
}

bool PWorld::OwnsLevel(const PLevel* Level) const
{
    if (Level == nullptr)
    {
        return false;
    }

    return std::any_of(
        LevelHandles.begin(),
        LevelHandles.end(),
        [this, Level](FObjectHandle Handle)
        {
            return ResolveLevel(Handle) == Level;
        });
}

bool PWorld::OwnsActor(const PActor* Actor) const
{
    if (Actor == nullptr)
    {
        return false;
    }

    PLevel* Level = Actor->GetLevel();
    return OwnsLevel(Level) && Level->OwnsActor(Actor);
}

void PWorld::BeginPlay()
{
    bHasBegunPlay = true;
    const std::vector<PLevel*> Levels = GetLevels();
    for (PLevel* Level : Levels)
    {
        if (Level == nullptr)
        {
            continue;
        }

        const std::vector<PActor*> Actors = Level->GetActors();
        for (PActor* Actor : Actors)
        {
            if (Actor != nullptr && OwnsActor(Actor))
            {
                Actor->DispatchBeginPlay();
            }
        }
    }
}

void PWorld::ProcessPendingDestroyActors()
{
    std::vector<FObjectHandle> PendingHandles = std::move(PendingDestroyActorHandles);
    PendingDestroyActorHandles.clear();

    for (const FObjectHandle Handle : PendingHandles)
    {
        PObject* Object = ResolveObject(Handle);
        if (Object != nullptr && Object->IsA(PActor::StaticClass()))
        {
            DestroyActorNow(static_cast<PActor*>(Object));
        }
    }
}

void PWorld::DestroyActorNow(PActor* Actor)
{
    if (Actor != nullptr)
    {
        DestroyObjectTree(Actor);
    }
}
}
