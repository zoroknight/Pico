#include "Pico/Engine/World.h"

#include "Pico/Core/ScopeExit.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Profiler.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ReferenceCollector.h"
#include "Pico/PhysicsCore/PhysicsScene.h"
#include "Pico/PhysicsJolt/JoltPhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PWorld)

PWorld::PWorld(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

PWorld::~PWorld() = default;

void PWorld::SetNetDriver(FNetDriver* InNetDriver)
{
    NetDriver = InNetDriver;
}

FNetDriver* PWorld::GetNetDriver() const
{
    return NetDriver;
}

void PWorld::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PObject::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(LevelHandles);
    Collector.AddReferencedHandles(PendingDestroyActorHandles);
    Collector.AddReferencedHandle(GameModeHandle);
    Collector.AddReferencedHandle(GameStateHandle);
}

bool PWorld::Initialize()
{
    if (!CheckGameThread("PWorld::Initialize"))
    {
        return false;
    }
    if (State != EWorldState::Uninitialized)
    {
        PICO_LOG(LogEngine, Error, "World '{}' cannot be initialized from its current state", GetPathName());
        return false;
    }

    if (!InitializePhysicsScene()) return false;

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

bool PWorld::InitializePhysicsScene()
{
    if (PhysicsScene != nullptr)
    {
        return PhysicsScene->IsValid();
    }

    PhysicsScene = CreateJoltPhysicsScene();
    if (PhysicsScene == nullptr || !PhysicsScene->IsValid())
    {
        PICO_LOG(LogEngine, Error, "World '{}' failed to initialize Jolt physics", GetPathName());
        PhysicsScene.reset();
        return false;
    }
    return true;
}

bool PWorld::InitializeGameplay(const PClass* GameModeClass)
{
    if (!CheckGameThread("PWorld::InitializeGameplay")
        || State != EWorldState::Initialized
        || GameModeHandle.IsValid()
        || GameModeClass == nullptr
        || !GameModeClass->IsChildOf(PGameModeBase::StaticClass()))
    {
        return false;
    }

    FActorSpawnParameters Parameters;
    Parameters.Name = FName("GameMode");
    Parameters.ObjectFlags = EObjectFlags::Transient;
    PActor* Actor = SpawnActor(GameModeClass, Parameters);
    PGameModeBase* GameMode = Actor != nullptr
        ? static_cast<PGameModeBase*>(Actor)
        : nullptr;
    PGameStateBase* GameState =
        GameMode != nullptr ? GameMode->CreateGameState() : nullptr;
    if (GameMode == nullptr || GameState == nullptr)
    {
        if (GameMode != nullptr)
        {
            DestroyActor(GameMode);
        }
        return false;
    }

    GameModeHandle = GameMode->GetHandle();
    GameStateHandle = GameState->GetHandle();
    return true;
}

void PWorld::Tick(float DeltaSeconds)
{
    PICO_PROFILE_SCOPE("World.Tick");
    if (!CheckGameThread("PWorld::Tick"))
    {
        return;
    }
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
    if (!TickTaskManager.BeginFrame(DeltaSeconds))
    {
        return;
    }
    auto EndTickFrame = MakeScopeExit(
        [this]()
        {
            TickTaskManager.EndFrame();
        });
    TickTaskManager.RunTickGroup(ETickGroup::PrePhysics);
    TickTaskManager.RunTickGroup(ETickGroup::DuringPhysics);
    if (PhysicsScene != nullptr)
    {
        PICO_PROFILE_SCOPE("Physics.Step");
        PhysicsStepCount += PhysicsScene->Step(DeltaSeconds);
        SyncDynamicPhysicsBodies();
        LastPhysicsEvents = PhysicsScene->DrainContactEvents();
        DispatchPhysicsEvents();
    }
    TickTaskManager.RunTickGroup(ETickGroup::PostPhysics);
    TickTaskManager.RunTickGroup(ETickGroup::PostUpdateWork);
    TickTaskManager.EndFrame();
    EndTickFrame.Release();
    ResetTickingActors.Release();
    bTickingActors = false;
    ProcessPendingDestroyActors();
}

void PWorld::TearDown()
{
    if (!CheckGameThread("PWorld::TearDown"))
    {
        return;
    }
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
    GameModeHandle = {};
    GameStateHandle = {};
    CollisionQuery = nullptr;
    LastPhysicsEvents.clear();
    ActiveOverlapPairs.clear();
    PhysicsScene.reset();
    TickTaskManager.Reset();
    State = EWorldState::TornDown;
    PICO_LOG(LogEngine, Info, "World '{}' torn down after {} ticks", GetPathName(), TickCount);
}

PLevel* PWorld::CreateLevel(FName Name)
{
    if (!CheckGameThread("PWorld::CreateLevel"))
    {
        return nullptr;
    }
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
    if (!CheckGameThread("PWorld::RemoveLevel"))
    {
        return false;
    }
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
    if (!CheckGameThread("PWorld::SetCurrentLevel"))
    {
        return false;
    }
    if (State != EWorldState::Initialized || !OwnsLevel(Level))
    {
        return false;
    }

    CurrentLevelHandle = Level->GetHandle();
    return true;
}

PActor* PWorld::SpawnActor(const PClass* ActorClass, const FActorSpawnParameters& SpawnParameters)
{
    if (!CheckGameThread("PWorld::SpawnActor"))
    {
        return nullptr;
    }
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

    if (SpawnParameters.Name.IsNone())
    {
        PICO_LOG(LogEngine, Error, "World '{}' requires a non-empty actor name", GetPathName());
        return nullptr;
    }

    PLevel* TargetLevel = SpawnParameters.OverrideLevel != nullptr
        ? SpawnParameters.OverrideLevel
        : GetCurrentLevel();
    if (!OwnsLevel(TargetLevel))
    {
        PICO_LOG(LogEngine, Error, "World '{}' cannot spawn an actor into an unowned level", GetPathName());
        return nullptr;
    }

    if (SpawnParameters.Owner != nullptr
        && (!OwnsActor(SpawnParameters.Owner)
            || SpawnParameters.Owner->IsPendingDestroy()
            || SpawnParameters.Owner->IsBeginningDestroy()))
    {
        PICO_LOG(LogEngine, Error, "World '{}' requires a live actor owner from the same World", GetPathName());
        return nullptr;
    }

    const FObjectConstructionParams ConstructionParams {
        ActorClass,
        TargetLevel,
        SpawnParameters.Name,
        SpawnParameters.ObjectFlags,
        nullptr
    };
    PObject* Object = NewObject(ConstructionParams);
    if (Object == nullptr)
    {
        return nullptr;
    }

    PActor* Actor = static_cast<PActor*>(Object);
    Actor->SetOwner(SpawnParameters.Owner);
    TargetLevel->AddActor(Actor);
    if (bHasBegunPlay)
    {
        Actor->DispatchBeginPlay();
    }

    const FObjectHandle SpawnedHandle = Actor->GetHandle();
    const std::string SpawnedPath = Actor->GetPathName();
    try
    {
        ActorSpawnedEvent.Broadcast(Actor);
    }
    catch (const std::exception& Exception)
    {
        PICO_LOG(
            LogEngine,
            Error,
            "OnActorSpawned listener for '{}' threw an exception: {}",
            SpawnedPath,
            Exception.what());
    }
    catch (...)
    {
        PICO_LOG(
            LogEngine,
            Error,
            "OnActorSpawned listener for '{}' threw an unknown exception",
            SpawnedPath);
    }

    PObject* LiveObject = ResolveObject(SpawnedHandle);
    PActor* LiveActor = LiveObject != nullptr
            && LiveObject->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(LiveObject)
        : nullptr;
    return LiveActor != nullptr
            && !LiveActor->IsPendingDestroy()
            && OwnsActor(LiveActor)
        ? LiveActor
        : nullptr;
}

PActor* PWorld::SpawnActor(const PClass* ActorClass, FName Name, PLevel* Level)
{
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = Name;
    SpawnParameters.OverrideLevel = Level;
    return SpawnActor(ActorClass, SpawnParameters);
}

PActor* PWorld::SpawnActor(const PClass* ActorClass, std::string_view Name, PLevel* Level)
{
    return SpawnActor(ActorClass, FName(Name), Level);
}

bool PWorld::DestroyActor(PActor* Actor)
{
    if (!CheckGameThread("PWorld::DestroyActor"))
    {
        return false;
    }
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
    Actor->DispatchDestroyed();

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

FOnActorSpawned& PWorld::OnActorSpawned()
{
    return ActorSpawnedEvent;
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

FTickTaskManager& PWorld::GetTickTaskManager() { return TickTaskManager; }
const FTickTaskManager& PWorld::GetTickTaskManager() const { return TickTaskManager; }

PGameModeBase* PWorld::GetGameMode() const
{
    PObject* Object = ResolveObject(GameModeHandle);
    return Object != nullptr && Object->IsA(PGameModeBase::StaticClass())
        ? static_cast<PGameModeBase*>(Object)
        : nullptr;
}

PGameStateBase* PWorld::GetGameState() const
{
    PObject* Object = ResolveObject(GameStateHandle);
    return Object != nullptr && Object->IsA(PGameStateBase::StaticClass())
        ? static_cast<PGameStateBase*>(Object)
        : nullptr;
}

void PWorld::BindReplicatedGameState(PGameStateBase* GameState)
{
    if (State == EWorldState::Initialized && !GameModeHandle.IsValid()
        && GameState != nullptr)
    {
        GameStateHandle = GameState->GetHandle();
    }
}

uint64 PWorld::GetPhysicsStepCount() const { return PhysicsStepCount; }
uint64 PWorld::GetPhysicsHitCount() const { return PhysicsHitCount; }
uint64 PWorld::GetPhysicsBeginOverlapCount() const { return PhysicsBeginOverlapCount; }
uint64 PWorld::GetPhysicsEndOverlapCount() const { return PhysicsEndOverlapCount; }

IPhysicsScene* PWorld::GetPhysicsScene() const
{
    return PhysicsScene.get();
}

void PWorld::SetPhysicsScene(std::unique_ptr<IPhysicsScene> InPhysicsScene)
{
    if (CheckGameThread("PWorld::SetPhysicsScene") && !bTickingActors && !bHasBegunPlay)
    {
        PhysicsScene = std::move(InPhysicsScene);
    }
}

IWorldCollisionQuery* PWorld::GetCollisionQuery() const
{
    return CollisionQuery != nullptr ? CollisionQuery : PhysicsScene.get();
}

void PWorld::SetCollisionQuery(IWorldCollisionQuery* InCollisionQuery)
{
    if (CheckGameThread("PWorld::SetCollisionQuery"))
    {
        CollisionQuery = InCollisionQuery;
    }
}

void PWorld::SetAssetServices(FAssetRegistry* InRegistry, FAssetManager* InManager)
{
    AssetRegistry = InRegistry;
    AssetManager = InManager;
}

FAssetRegistry* PWorld::GetAssetRegistry() const { return AssetRegistry; }
FAssetManager* PWorld::GetAssetManager() const { return AssetManager; }

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
        if (Actor->GetHandle() == GameModeHandle) GameModeHandle = {};
        if (Actor->GetHandle() == GameStateHandle) GameStateHandle = {};
        DestroyObjectTree(Actor);
    }
}


void PWorld::SyncDynamicPhysicsBodies()
{
    for (PLevel* Level : GetLevels())
    {
        if (Level == nullptr) continue;
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr) continue;
            for (PActorComponent* Component : Actor->GetComponents())
            {
                if (Component != nullptr && Component->IsA(PPrimitiveComponent::StaticClass()))
                {
                    static_cast<PPrimitiveComponent*>(Component)->SyncComponentFromPhysics();
                }
            }
        }
    }
}

void PWorld::DispatchPhysicsEvents()
{
    for (const FPhysicsContactEvent& RawEvent : LastPhysicsEvents)
    {
        FPhysicsContactEvent Event = RawEvent;
        PObject* ObjectA = ResolveObject(Event.ObjectA);
        PObject* ObjectB = ResolveObject(Event.ObjectB);
        PPrimitiveComponent* ComponentA = ObjectA != nullptr
                && ObjectA->IsA(PPrimitiveComponent::StaticClass())
            ? static_cast<PPrimitiveComponent*>(ObjectA)
            : nullptr;
        PPrimitiveComponent* ComponentB = ObjectB != nullptr
                && ObjectB->IsA(PPrimitiveComponent::StaticClass())
            ? static_cast<PPrimitiveComponent*>(ObjectB)
            : nullptr;

        if (Event.bSensor)
        {
            const auto IsSamePair = [&Event](const FActiveOverlapPair& Pair)
            {
                return (Pair.A == Event.ObjectA && Pair.B == Event.ObjectB)
                    || (Pair.A == Event.ObjectB && Pair.B == Event.ObjectA);
            };
            const auto Active = std::find_if(
                ActiveOverlapPairs.begin(), ActiveOverlapPairs.end(), IsSamePair);

            if (Event.Type == EPhysicsContactEventType::Begin
                || Event.Type == EPhysicsContactEventType::Persist)
            {
                if (Active != ActiveOverlapPairs.end()) continue;
                ActiveOverlapPairs.push_back({Event.ObjectA, Event.ObjectB});
                Event.Type = EPhysicsContactEventType::Begin;
                ++PhysicsBeginOverlapCount;
            }
            else
            {
                if (Active == ActiveOverlapPairs.end()) continue;

                bool bStillOverlapping = false;
                IWorldCollisionQuery* Query = GetCollisionQuery();
                if (ComponentA != nullptr && ComponentB != nullptr && Query != nullptr)
                {
                    FCollisionQueryParams Params;
                    Params.MovingObject = ComponentA->GetHandle();
                    Params.bIgnoreSensors = false;
                    std::vector<FOverlapResult> Overlaps;
                    if (Query->Overlap(
                            ComponentA->GetCollisionShape(),
                            ComponentA->GetWorldTransform().Translation,
                            ComponentA->GetWorldTransform().Rotation,
                            Params,
                            Overlaps))
                    {
                        bStillOverlapping = std::any_of(
                            Overlaps.begin(), Overlaps.end(),
                            [ComponentB](const FOverlapResult& Result)
                            {
                                return Result.OverlapObject == ComponentB->GetHandle();
                            });
                    }
                }
                if (bStillOverlapping) continue;
                ActiveOverlapPairs.erase(Active);
                ++PhysicsEndOverlapCount;
            }
        }
        else if (Event.Type == EPhysicsContactEventType::Begin)
        {
            ++PhysicsHitCount;
        }
        const auto Dispatch = [&Event](PPrimitiveComponent* Component, PPrimitiveComponent* Other)
        {
            if (Component == nullptr) return;
            try
            {
                Component->DispatchPhysicsEvent(Other, Event);
            }
            catch (const std::exception& Exception)
            {
                PICO_LOG(LogEngine, Error, "Physics event listener threw: {}", Exception.what());
            }
            catch (...)
            {
                PICO_LOG(LogEngine, Error, "Physics event listener threw an unknown exception");
            }
        };
        Dispatch(ComponentA, ComponentB);
        Dispatch(ComponentB, ComponentA);
    }
}
}
