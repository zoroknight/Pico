#pragma once

#include "Pico/Engine/GameInstance.h"

#include <string>
#include <vector>

namespace PicoSandbox
{
class PSandboxReplicationLabActor;

class PSandboxGameInstance final : public Pico::PGameInstance
{
    PICO_DECLARE_CLASS(PSandboxGameInstance, Pico::PGameInstance)

public:
    bool Init(Pico::FGameEngine& GameEngine) override;
    void OnWorldInitialized(Pico::PWorld* World) override;
    void Tick(float DeltaSeconds) override;
    void OnWorldCleanup(Pico::PWorld* World) override;
    void Shutdown() override;
    void AppendGameplayDebugLines(std::vector<std::string>& OutLines) const override;
    void AppendGameplayStatusLines(std::vector<std::string>& OutLines) const override;
    void AppendGameplayAbilityStatus(
        std::vector<Pico::FPlayerAbilityStatus>& OutPlayers) const override;

private:
    explicit PSandboxGameInstance(const Pico::FObjectConstructionParams& Params);
    PSandboxReplicationLabActor* ResolveReplicationLabActor() const;
    PSandboxReplicationLabActor* FindReplicationLabActor() const;
    bool SpawnReplicationLabActor();
    void RemoveUnpossessedServerPreviewPawns(Pico::PWorld* World);

    Pico::FObjectHandle ReplicationLabActorHandle;
    std::string ReplicationLabLastAction = "waiting for world";
    Pico::int32 ReplicationLabMoveStep = 0;
};
}
