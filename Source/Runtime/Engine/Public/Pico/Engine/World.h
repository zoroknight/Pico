#pragma once

#include "Pico/Core/Types.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Engine/TickTaskManager.h"
#include "Pico/PhysicsCore/CollisionTypes.h"

#include <string_view>
#include <memory>
#include <type_traits>
#include <vector>

namespace Pico
{
class PActor;
class FAssetManager;
class FAssetRegistry;
class FWorldAssetLoader;
class FReferenceCollector;
class PPrimitiveComponent;
class PGameModeBase;
class PGameStateBase;
class PLevel;
class IWorldCollisionQuery;
class IPhysicsScene;

using FOnActorSpawned = TObjectMulticastDelegate<void(PActor*)>;

struct FActorSpawnParameters
{
    FName Name;
    PLevel* OverrideLevel = nullptr;
    PActor* Owner = nullptr;
    EObjectFlags ObjectFlags = EObjectFlags::None;
};

enum class EWorldState
{
    Uninitialized,
    Initialized,
    TearingDown,
    TornDown
};

class PWorld final : public PObject
{
    PICO_DECLARE_CLASS(PWorld, PObject)

public:
    bool Initialize();
    bool InitializeGameplay(const PClass* GameModeClass);
    void Tick(float DeltaSeconds);
    void TearDown();

    PLevel* CreateLevel(FName Name);
    PLevel* CreateLevel(std::string_view Name);
    bool RemoveLevel(PLevel* Level);
    bool SetCurrentLevel(PLevel* Level);
    PActor* SpawnActor(const PClass* ActorClass, const FActorSpawnParameters& SpawnParameters);
    PActor* SpawnActor(const PClass* ActorClass, FName Name, PLevel* Level = nullptr);
    PActor* SpawnActor(const PClass* ActorClass, std::string_view Name, PLevel* Level = nullptr);
    bool DestroyActor(PActor* Actor);
    FOnActorSpawned& OnActorSpawned();

    template <typename TActor>
    TActor* SpawnActor(const FActorSpawnParameters& SpawnParameters)
    {
        static_assert(std::is_base_of_v<PActor, TActor>, "SpawnActor only constructs PActor-derived types");
        return static_cast<TActor*>(SpawnActor(TActor::StaticClass(), SpawnParameters));
    }

    template <typename TActor>
    TActor* SpawnActor(FName Name, PLevel* Level = nullptr)
    {
        static_assert(std::is_base_of_v<PActor, TActor>, "SpawnActor only constructs PActor-derived types");
        return static_cast<TActor*>(SpawnActor(TActor::StaticClass(), Name, Level));
    }

    template <typename TActor>
    TActor* SpawnActor(std::string_view Name, PLevel* Level = nullptr)
    {
        return SpawnActor<TActor>(FName(Name), Level);
    }

    PLevel* GetPersistentLevel() const;
    PLevel* GetCurrentLevel() const;
    std::vector<PLevel*> GetLevels() const;
    EWorldState GetState() const;
    uint64 GetTickCount() const;
    double GetTimeSeconds() const;
    FTickTaskManager& GetTickTaskManager();
    const FTickTaskManager& GetTickTaskManager() const;
    PGameModeBase* GetGameMode() const;
    PGameStateBase* GetGameState() const;
    uint64 GetPhysicsStepCount() const;
    uint64 GetPhysicsHitCount() const;
    uint64 GetPhysicsBeginOverlapCount() const;
    uint64 GetPhysicsEndOverlapCount() const;
    IPhysicsScene* GetPhysicsScene() const;
    void SetPhysicsScene(std::unique_ptr<IPhysicsScene> InPhysicsScene);
    IWorldCollisionQuery* GetCollisionQuery() const;
    void SetCollisionQuery(IWorldCollisionQuery* InCollisionQuery);
    void SetAssetServices(FAssetRegistry* InRegistry, FAssetManager* InManager);
    FAssetRegistry* GetAssetRegistry() const;
    FAssetManager* GetAssetManager() const;

protected:
    explicit PWorld(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;
    ~PWorld() override;

private:
    struct FActiveOverlapPair
    {
        FObjectHandle A;
        FObjectHandle B;
    };

    bool InitializePhysicsScene();
    PLevel* ResolveLevel(FObjectHandle Handle) const;
    bool OwnsLevel(const PLevel* Level) const;
    bool OwnsActor(const PActor* Actor) const;
    void BeginPlay();
    void ProcessPendingDestroyActors();
    void DestroyActorNow(PActor* Actor);
    void SyncDynamicPhysicsBodies();
    void DispatchPhysicsEvents();

    std::vector<FObjectHandle> LevelHandles;
    std::vector<FObjectHandle> PendingDestroyActorHandles;
    FObjectHandle PersistentLevelHandle;
    FObjectHandle CurrentLevelHandle;
    FObjectHandle GameModeHandle;
    FObjectHandle GameStateHandle;
    FOnActorSpawned ActorSpawnedEvent;
    FTickTaskManager TickTaskManager;
    std::unique_ptr<IPhysicsScene> PhysicsScene;
    IWorldCollisionQuery* CollisionQuery = nullptr;
    FAssetRegistry* AssetRegistry = nullptr;
    FAssetManager* AssetManager = nullptr;
    std::vector<FPhysicsContactEvent> LastPhysicsEvents;
    std::vector<FActiveOverlapPair> ActiveOverlapPairs;
    EWorldState State = EWorldState::Uninitialized;
    uint64 TickCount = 0;
    uint64 PhysicsStepCount = 0;
    uint64 PhysicsHitCount = 0;
    uint64 PhysicsBeginOverlapCount = 0;
    uint64 PhysicsEndOverlapCount = 0;
    double TimeSeconds = 0.0;
    bool bHasBegunPlay = false;
    bool bTickingActors = false;

    friend class FWorldAssetLoader;
};
}
