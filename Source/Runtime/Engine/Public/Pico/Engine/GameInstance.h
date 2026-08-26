#pragma once

#include "Pico/Object/Object.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Net/NetTypes.h"

#include <span>
#include <string>
#include <vector>

namespace Pico
{
class FGameEngine;
class FReferenceCollector;
class PLocalPlayer;
class PNetPlayer;
class PWorld;

class PGameInstance : public PObject
{
    PICO_DECLARE_CLASS(PGameInstance, PObject)

public:
    virtual bool Init(FGameEngine& GameEngine);
    virtual void OnWorldInitialized(PWorld* World);
    virtual void Tick(float DeltaSeconds);
    virtual void OnWorldCleanup(PWorld* World);
    virtual void Shutdown();
    virtual void AppendGameplayDebugLines(std::vector<std::string>& OutLines) const;
    virtual void AppendGameplayStatusLines(std::vector<std::string>& OutLines) const;

    FGameEngine* GetGameEngine() const;
    PWorld* GetWorld() const;
    const std::vector<PLocalPlayer*>& GetLocalPlayers() const;
    PLocalPlayer* GetPrimaryLocalPlayer() const;

protected:
    explicit PGameInstance(const FObjectConstructionParams& Params);
    void AddReferencedObjects(FReferenceCollector& Collector) const override;
    void BeginDestroy() override;

private:
    friend class FGameEngine;

    bool DispatchInit(FGameEngine& GameEngine);
    void DispatchWorldInitialized(PWorld* World);
    void DispatchWorldCleanup(PWorld* World);
    void DispatchShutdown();
    PLocalPlayer* CreateLocalPlayer();
    void LoginLocalPlayers(PWorld* World);
    void LogoutLocalPlayers(PWorld* World);
    void RefreshLocalPlayers() const;
    void DispatchNetworkEvents(
        std::span<const FNetConnectionId> Opened,
        std::span<const FNetConnectionId> Closed);
    void LoginNetworkPlayer(FNetConnectionId ConnectionId);
    void LogoutNetworkPlayer(FNetConnectionId ConnectionId);
    void RefreshClientControllerBinding();

    FGameEngine* OwningGameEngine = nullptr;
    FObjectHandle WorldHandle;
    std::vector<FObjectHandle> LocalPlayerHandles;
    std::vector<FObjectHandle> NetPlayerHandles;
    mutable std::vector<PLocalPlayer*> LocalPlayerCache;
    bool bInitialized = false;
};
}
