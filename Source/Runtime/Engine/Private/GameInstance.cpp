#include "Pico/Engine/GameInstance.h"

#include "Pico/Engine/World.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ReferenceCollector.h"

#include <string>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PGameInstance)

PGameInstance::PGameInstance(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

bool PGameInstance::Init(FGameEngine&)
{
    return true;
}

void PGameInstance::OnWorldInitialized(PWorld*)
{
}

void PGameInstance::Tick(float)
{
}

void PGameInstance::OnWorldCleanup(PWorld*)
{
}

void PGameInstance::Shutdown()
{
}

FGameEngine* PGameInstance::GetGameEngine() const
{
    return OwningGameEngine;
}

PWorld* PGameInstance::GetWorld() const
{
    if (!WorldHandle.IsValid())
    {
        return nullptr;
    }
    PObject* Object = ResolveObject(WorldHandle);
    return Object != nullptr && Object->IsA(PWorld::StaticClass())
        ? static_cast<PWorld*>(Object)
        : nullptr;
}

const std::vector<PLocalPlayer*>& PGameInstance::GetLocalPlayers() const
{
    RefreshLocalPlayers();
    return LocalPlayerCache;
}

PLocalPlayer* PGameInstance::GetPrimaryLocalPlayer() const
{
    const std::vector<PLocalPlayer*>& Players = GetLocalPlayers();
    return Players.empty() ? nullptr : Players.front();
}

PLocalPlayer* PGameInstance::CreateLocalPlayer()
{
    const int32 Index = static_cast<int32>(LocalPlayerHandles.size());
    PLocalPlayer* LocalPlayer = NewObject<PLocalPlayer>(
        this,
        "LocalPlayer_" + std::to_string(Index));
    if (LocalPlayer == nullptr)
    {
        return nullptr;
    }
    LocalPlayer->SetLocalPlayerIndex(Index);
    LocalPlayerHandles.push_back(LocalPlayer->GetHandle());
    LocalPlayerCache.clear();
    return LocalPlayer;
}

void PGameInstance::LoginLocalPlayers(PWorld* World)
{
    PGameModeBase* GameMode = World != nullptr ? World->GetGameMode() : nullptr;
    if (GameMode == nullptr)
    {
        return;
    }
    for (PLocalPlayer* LocalPlayer : GetLocalPlayers())
    {
        if (LocalPlayer == nullptr || LocalPlayer->GetPlayerController() != nullptr)
        {
            continue;
        }
        PPlayerController* Controller = GameMode->Login(LocalPlayer);
        if (Controller != nullptr)
        {
            GameMode->PostLogin(Controller);
            GameMode->HandleStartingNewPlayer(Controller);
        }
    }
}

void PGameInstance::LogoutLocalPlayers(PWorld* World)
{
    PGameModeBase* GameMode = World != nullptr ? World->GetGameMode() : nullptr;
    for (PLocalPlayer* LocalPlayer : GetLocalPlayers())
    {
        if (LocalPlayer == nullptr)
        {
            continue;
        }
        PPlayerController* Controller = LocalPlayer->GetPlayerController();
        if (GameMode != nullptr && Controller != nullptr)
        {
            GameMode->Logout(Controller);
        }
        LocalPlayer->SetPlayerController(nullptr);
    }
}

void PGameInstance::RefreshLocalPlayers() const
{
    LocalPlayerCache.clear();
    for (FObjectHandle Handle : LocalPlayerHandles)
    {
        PObject* Object = ResolveObject(Handle);
        if (Object != nullptr && Object->IsA(PLocalPlayer::StaticClass()))
        {
            LocalPlayerCache.push_back(static_cast<PLocalPlayer*>(Object));
        }
    }
}

void PGameInstance::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PObject::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(LocalPlayerHandles);
}

void PGameInstance::BeginDestroy()
{
    LocalPlayerCache.clear();
    LocalPlayerHandles.clear();
    PObject::BeginDestroy();
}

bool PGameInstance::DispatchInit(FGameEngine& InGameEngine)
{
    if (bInitialized)
    {
        return false;
    }
    OwningGameEngine = &InGameEngine;
    bInitialized = CreateLocalPlayer() != nullptr && Init(InGameEngine);
    if (!bInitialized)
    {
        OwningGameEngine = nullptr;
    }
    return bInitialized;
}

void PGameInstance::DispatchWorldInitialized(PWorld* World)
{
    if (!bInitialized || World == nullptr)
    {
        return;
    }
    WorldHandle = World->GetHandle();
    LoginLocalPlayers(World);
    OnWorldInitialized(World);
}

void PGameInstance::DispatchWorldCleanup(PWorld* World)
{
    if (!bInitialized || World == nullptr || GetWorld() != World)
    {
        return;
    }
    LogoutLocalPlayers(World);
    OnWorldCleanup(World);
    WorldHandle = {};
    LocalPlayerCache.clear();
}

void PGameInstance::DispatchShutdown()
{
    if (bInitialized)
    {
        Shutdown();
    }
    WorldHandle = {};
    OwningGameEngine = nullptr;
    bInitialized = false;
}
}
