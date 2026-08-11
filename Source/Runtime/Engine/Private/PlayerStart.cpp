#include "Pico/Engine/PlayerStart.h"

#include "Pico/Engine/SceneComponent.h"
#include "Pico/Object/ObjectInitializer.h"

namespace Pico
{
PICO_DEFINE_CLASS(PPlayerStart)

bool PPlayerStart::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, PlayerStartId);
    PICO_ADD_PROPERTY(Properties, OnPlayerSpawnedEvent);
    FPropertyMetadata CountMetadata;
    CountMetadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    PICO_ADD_PROPERTY_METADATA(Properties, SpawnEventCount, CountMetadata);

    std::vector<PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions,
        RecordPlayerSpawn,
        EFunctionFlags::Callable,
        FName("SpawnedPawn"));
    return Class.AddProperties(std::move(Properties))
        && Class.AddFunctions(std::move(Functions));
}

PPlayerStart::PPlayerStart(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

bool PPlayerStart::DefineDefaultSubobjects(FObjectInitializer& Initializer)
{
    PSceneComponent* Root =
        Initializer.CreateDefaultSubobject<PSceneComponent>("DefaultSceneRoot");
    return Root != nullptr && Initializer.SetRootSubobject(Root);
}

int32 PPlayerStart::GetPlayerStartId() const { return PlayerStartId; }
void PPlayerStart::SetPlayerStartId(int32 InId) { PlayerStartId = InId; }
int32 PPlayerStart::GetSpawnEventCount() const { return SpawnEventCount; }

TDynamicMulticastDelegate<void(PPawn*)>& PPlayerStart::OnPlayerSpawned()
{
    return OnPlayerSpawnedEvent;
}

void PPlayerStart::NotifyPlayerSpawned(PPawn* SpawnedPawn)
{
    OnPlayerSpawnedEvent.Broadcast(SpawnedPawn);
}

void PPlayerStart::RecordPlayerSpawn(PPawn* SpawnedPawn)
{
    if (SpawnedPawn != nullptr)
    {
        ++SpawnEventCount;
    }
}
}
