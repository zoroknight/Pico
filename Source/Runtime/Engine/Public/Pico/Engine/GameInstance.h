#pragma once

#include "Pico/Object/Object.h"
#include "Pico/Object/ReflectionMacros.h"

#include <string>
#include <vector>

namespace Pico
{
class FGameEngine;
class FReferenceCollector;
class PLocalPlayer;
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

    FGameEngine* OwningGameEngine = nullptr;
    FObjectHandle WorldHandle;
    std::vector<FObjectHandle> LocalPlayerHandles;
    mutable std::vector<PLocalPlayer*> LocalPlayerCache;
    bool bInitialized = false;
};
}
