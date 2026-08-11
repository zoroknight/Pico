#pragma once

#include "Pico/Engine/Pawn.h"
#include "Pico/Object/DynamicMulticastDelegate.h"

namespace Pico
{
class PPlayerStart : public PActor
{
    PICO_DECLARE_CLASS(PPlayerStart, PActor)

public:
    int32 GetPlayerStartId() const;
    void SetPlayerStartId(int32 InId);
    int32 GetSpawnEventCount() const;
    TDynamicMulticastDelegate<void(PPawn*)>& OnPlayerSpawned();
    void NotifyPlayerSpawned(PPawn* SpawnedPawn);

protected:
    explicit PPlayerStart(const FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(FObjectInitializer& Initializer) override;

private:
    void RecordPlayerSpawn(PPawn* SpawnedPawn);

    int32 PlayerStartId = 0;
    int32 SpawnEventCount = 0;
    TDynamicMulticastDelegate<void(PPawn*)> OnPlayerSpawnedEvent;
};
}
