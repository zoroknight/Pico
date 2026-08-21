#include "Pico/Engine/GameInstance.h"

#include "Pico/Core/Log.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/NetPlayer.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/Pawn.h"
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

void PGameInstance::AppendGameplayDebugLines(std::vector<std::string>&) const
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
            GameMode->DispatchPostLogin(Controller);
            GameMode->HandleStartingNewPlayer(Controller);
        }
    }
    GameMode->StartPlay();
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

void PGameInstance::DispatchNetworkEvents(
    std::span<const FNetConnectionId> Opened,
    std::span<const FNetConnectionId> Closed)
{
    if (!bInitialized || OwningGameEngine == nullptr) return;
    if (OwningGameEngine->GetNetDriver().GetNetMode() == ENetMode::Server)
    {
        for (FNetConnectionId ConnectionId : Closed)
            LogoutNetworkPlayer(ConnectionId);
        for (FNetConnectionId ConnectionId : Opened)
            LoginNetworkPlayer(ConnectionId);
    }
    else if (OwningGameEngine->GetNetDriver().GetNetMode() == ENetMode::Client)
    {
        RefreshClientControllerBinding();
    }
}

void PGameInstance::LoginNetworkPlayer(FNetConnectionId ConnectionId)
{
    PWorld* World = GetWorld();
    PGameModeBase* GameMode = World != nullptr ? World->GetGameMode() : nullptr;
    if (!ConnectionId.IsValid() || GameMode == nullptr) return;
    for (FObjectHandle Handle : NetPlayerHandles)
    {
        PObject* Object = ResolveObject(Handle);
        if (Object != nullptr && Object->IsA(PNetPlayer::StaticClass())
            && static_cast<PNetPlayer*>(Object)->GetConnectionId() == ConnectionId)
            return;
    }

    PNetPlayer* NetPlayer = NewObject<PNetPlayer>(
        this, "NetPlayer_" + std::to_string(ConnectionId.Value));
    if (NetPlayer == nullptr) return;
    NetPlayer->SetConnectionId(ConnectionId);
    PPlayerController* Controller = GameMode->Login(NetPlayer);
    if (Controller == nullptr)
    {
        DestroyObjectTree(NetPlayer);
        return;
    }
    Controller->SetReplicates(true);
    Controller->SetOnlyRelevantToOwner(true);
    GameMode->DispatchPostLogin(Controller);
    GameMode->HandleStartingNewPlayer(Controller);
    if (PPlayerState* State = Controller->GetPlayerState())
        State->SetReplicates(true);
    if (PPawn* Pawn = Controller->GetPawn()) Pawn->SetReplicates(true);
    OwningGameEngine->GetNetDriver().SetActorOwningConnection(
        Controller, ConnectionId);
    NetPlayerHandles.push_back(NetPlayer->GetHandle());
}

void PGameInstance::LogoutNetworkPlayer(FNetConnectionId ConnectionId)
{
    PWorld* World = GetWorld();
    PGameModeBase* GameMode = World != nullptr ? World->GetGameMode() : nullptr;
    for (auto It = NetPlayerHandles.begin(); It != NetPlayerHandles.end();)
    {
        PObject* Object = ResolveObject(*It);
        PNetPlayer* NetPlayer = Object != nullptr
                && Object->IsA(PNetPlayer::StaticClass())
            ? static_cast<PNetPlayer*>(Object) : nullptr;
        if (NetPlayer == nullptr)
        {
            It = NetPlayerHandles.erase(It);
            continue;
        }
        if (NetPlayer->GetConnectionId() != ConnectionId)
        {
            ++It;
            continue;
        }
        if (GameMode != nullptr && NetPlayer->GetPlayerController() != nullptr)
            GameMode->Logout(NetPlayer->GetPlayerController());
        DestroyObjectTree(NetPlayer);
        It = NetPlayerHandles.erase(It);
    }
}

void PGameInstance::RefreshClientControllerBinding()
{
    PWorld* World = GetWorld();
    PLocalPlayer* LocalPlayer = GetPrimaryLocalPlayer();
    if (World == nullptr || LocalPlayer == nullptr) return;
    PPlayerController* ExistingController =
        LocalPlayer->GetPlayerController();
    if (ExistingController != nullptr
        && ExistingController->GetLocalRole() == ENetRole::AutonomousProxy)
    {
        PPawn* ExistingPawn = ExistingController->GetPawn();
        if (ExistingPawn != nullptr)
        {
            ExistingController->Possess(ExistingPawn);
        }
        return;
    }
    PPlayerController* AutonomousController = nullptr;
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr
                && Actor->IsA(PPlayerController::StaticClass())
                && Actor->GetLocalRole() == ENetRole::AutonomousProxy)
            {
                AutonomousController = static_cast<PPlayerController*>(Actor);
                break;
            }
        }
        if (AutonomousController != nullptr) break;
    }
    if (AutonomousController == nullptr) return;
    if (LocalPlayer->GetPlayerController() != AutonomousController)
    {
        PPlayerController* Previous = LocalPlayer->GetPlayerController();
        if (Previous != nullptr && World->GetGameMode() != nullptr
            && Previous->GetLocalRole() == ENetRole::Authority)
        {
            World->GetGameMode()->Logout(Previous);
        }
        LocalPlayer->SetPlayerController(AutonomousController);
        PICO_LOG(LogNet, Info,
            "Local player bound to autonomous controller '{}'",
            AutonomousController->GetPathName());
    }
    PPawn* Pawn = AutonomousController->GetPawn();
    if (Pawn != nullptr)
    {
        const bool bPossessed = AutonomousController->Possess(Pawn);
        PICO_LOG(LogNet, Info,
            "Client possess reconciliation: controller='{}' pawn='{}' result={}",
            AutonomousController->GetPathName(), Pawn->GetPathName(), bPossessed);
    }
}

void PGameInstance::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PObject::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(LocalPlayerHandles);
    Collector.AddReferencedHandles(NetPlayerHandles);
}

void PGameInstance::BeginDestroy()
{
    LocalPlayerCache.clear();
    LocalPlayerHandles.clear();
    NetPlayerHandles.clear();
    PObject::BeginDestroy();
}

bool PGameInstance::DispatchInit(FGameEngine& InGameEngine)
{
    if (bInitialized)
    {
        return false;
    }
    OwningGameEngine = &InGameEngine;
    const bool bNeedsLocalPlayer =
        InGameEngine.GetNetDriver().GetNetMode() != ENetMode::Server;
    bInitialized = (!bNeedsLocalPlayer || CreateLocalPlayer() != nullptr)
        && Init(InGameEngine);
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
    PGameModeBase* GameMode = World->GetGameMode();
    for (FObjectHandle Handle : NetPlayerHandles)
    {
        PObject* Object = ResolveObject(Handle);
        PNetPlayer* NetPlayer = Object != nullptr
                && Object->IsA(PNetPlayer::StaticClass())
            ? static_cast<PNetPlayer*>(Object) : nullptr;
        if (NetPlayer != nullptr)
        {
            if (GameMode != nullptr && NetPlayer->GetPlayerController() != nullptr)
                GameMode->Logout(NetPlayer->GetPlayerController());
            DestroyObjectTree(NetPlayer);
        }
    }
    NetPlayerHandles.clear();
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
