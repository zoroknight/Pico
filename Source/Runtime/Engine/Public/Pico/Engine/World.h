#pragma once

#include "Pico/Core/Types.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ReflectionMacros.h"

#include <string_view>
#include <type_traits>
#include <vector>

namespace Pico
{
class PActor;
class FWorldAssetLoader;
class FReferenceCollector;
class PLevel;

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

protected:
    explicit PWorld(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;

private:
    PLevel* ResolveLevel(FObjectHandle Handle) const;
    bool OwnsLevel(const PLevel* Level) const;
    bool OwnsActor(const PActor* Actor) const;
    void BeginPlay();
    void ProcessPendingDestroyActors();
    void DestroyActorNow(PActor* Actor);

    std::vector<FObjectHandle> LevelHandles;
    std::vector<FObjectHandle> PendingDestroyActorHandles;
    FObjectHandle PersistentLevelHandle;
    FObjectHandle CurrentLevelHandle;
    FOnActorSpawned ActorSpawnedEvent;
    EWorldState State = EWorldState::Uninitialized;
    uint64 TickCount = 0;
    double TimeSeconds = 0.0;
    bool bHasBegunPlay = false;
    bool bTickingActors = false;

    friend class FWorldAssetLoader;
};
}
