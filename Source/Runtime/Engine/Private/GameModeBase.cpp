#include "Pico/Engine/GameModeBase.h"

#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Core/Log.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Pico
{
namespace
{
FName MakeUniqueActorName(PWorld& World, std::string BaseName)
{
    PLevel* Level = World.GetCurrentLevel();
    if (Level == nullptr)
    {
        return FName(BaseName);
    }
    for (uint32 Suffix = 0; Suffix < 100000; ++Suffix)
    {
        const std::string Candidate = Suffix == 0
            ? BaseName
            : BaseName + "_" + std::to_string(Suffix);
        if (FindObject(Level, FName(Candidate)) == nullptr)
        {
            return FName(Candidate);
        }
    }
    return FName();
}

std::vector<PPlayerStart*> CollectPlayerStarts(PWorld* World)
{
    std::vector<PPlayerStart*> Result;
    if (World == nullptr) return Result;
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr && !Actor->IsPendingDestroy()
                && Actor->IsA(PPlayerStart::StaticClass()))
            {
                Result.push_back(static_cast<PPlayerStart*>(Actor));
            }
        }
    }
    return Result;
}

int32 GetPlayerStartIndex(PController* Controller)
{
    const auto* PlayerController = Controller != nullptr
            && Controller->IsA(PPlayerController::StaticClass())
        ? static_cast<const PPlayerController*>(Controller) : nullptr;
    const PPlayerState* State = PlayerController != nullptr
        ? PlayerController->GetPlayerState() : nullptr;
    return State != nullptr ? std::max(0, State->GetPlayerId()) : 0;
}

bool IsPlayerSpawnOccupied(
    PWorld* World,
    const PPawn* SpawnedPawn,
    const FVector3& Location,
    float MinimumSeparation)
{
    if (World == nullptr) return false;
    const float MinimumDistanceSquared = MinimumSeparation * MinimumSeparation;
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (PActor* Actor : Level->GetActors())
        {
            const auto* Pawn = Actor != nullptr
                    && Actor != SpawnedPawn
                    && Actor->IsA(PPawn::StaticClass())
                ? static_cast<const PPawn*>(Actor) : nullptr;
            if (Pawn != nullptr && !Pawn->IsPendingDestroy()
                && Pawn->GetController() != nullptr
                && (Pawn->GetActorLocation() - Location).SizeSquared()
                    < MinimumDistanceSquared)
            {
                return true;
            }
        }
    }
    return false;
}
}

PICO_DEFINE_CLASS(PGameModeBase)

bool PGameModeBase::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, GameState, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PGameModeBase::PGameModeBase(const FObjectConstructionParams& Params)
    : PActor(Params)
    , DefaultPawnClass(PPawn::StaticClass())
    , PlayerControllerClass(PPlayerController::StaticClass())
    , PlayerStateClass(PPlayerState::StaticClass())
    , GameStateClass(PGameStateBase::StaticClass())
{
    PrimaryActorTick.SetCanEverTick(false);
}

const PGameModeBase* PGameModeBase::GetClassDefaults() const
{
    if (HasAnyFlags(GetFlags(), EObjectFlags::ClassDefaultObject))
    {
        return this;
    }
    const PObject* Defaults = GetClass()->GetDefaultObject();
    return Defaults != nullptr && Defaults->IsA(PGameModeBase::StaticClass())
        ? static_cast<const PGameModeBase*>(Defaults)
        : this;
}

bool PGameModeBase::CanEditClassDefaults() const
{
    return HasAnyFlags(GetFlags(), EObjectFlags::ClassDefaultObject);
}

const PClass* PGameModeBase::GetDefaultPawnClass() const { return GetClassDefaults()->DefaultPawnClass; }
const PClass* PGameModeBase::GetPlayerControllerClass() const { return GetClassDefaults()->PlayerControllerClass; }
const PClass* PGameModeBase::GetPlayerStateClass() const { return GetClassDefaults()->PlayerStateClass; }
const PClass* PGameModeBase::GetGameStateClass() const { return GetClassDefaults()->GameStateClass; }
PGameStateBase* PGameModeBase::GetGameState() const { return GameState.Get(); }
EMatchState PGameModeBase::GetMatchState() const
{
    return GameState.Get() != nullptr
        ? GameState->GetMatchState()
        : EMatchState::EnteringMap;
}

bool PGameModeBase::HasMatchStarted() const
{
    const EMatchState State = GetMatchState();
    return State == EMatchState::InProgress
        || State == EMatchState::WaitingPostMatch
        || State == EMatchState::LeavingMap;
}

bool PGameModeBase::HasMatchEnded() const
{
    const EMatchState State = GetMatchState();
    return State == EMatchState::WaitingPostMatch
        || State == EMatchState::LeavingMap
        || State == EMatchState::Aborted;
}

PPlayerController* PGameModeBase::Login(PPlayer* NewPlayer)
{
    PWorld* World = GetWorld();
    PGameStateBase* CurrentGameState = GetGameState();
    if (World == nullptr
        || NewPlayer == nullptr
        || NewPlayer->IsBeginningDestroy()
        || NewPlayer->GetPlayerController() != nullptr
        || CurrentGameState == nullptr)
    {
        return nullptr;
    }

    const PClass* ControllerClass = GetPlayerControllerClass();
    const PClass* StateClass = GetPlayerStateClass();
    if (ControllerClass == nullptr || StateClass == nullptr)
    {
        return nullptr;
    }

    FActorSpawnParameters ControllerParameters;
    ControllerParameters.Name = MakeUniqueActorName(*World, "PlayerController");
    ControllerParameters.Owner = this;
    ControllerParameters.ObjectFlags = EObjectFlags::Transient;
    PActor* ControllerActor = World->SpawnActor(
        ControllerClass, ControllerParameters);
    PPlayerController* Controller = ControllerActor != nullptr
            && ControllerActor->IsA(PPlayerController::StaticClass())
        ? static_cast<PPlayerController*>(ControllerActor)
        : nullptr;
    if (Controller == nullptr)
    {
        return nullptr;
    }

    FActorSpawnParameters StateParameters;
    StateParameters.Name = MakeUniqueActorName(*World, "PlayerState");
    StateParameters.Owner = Controller;
    StateParameters.ObjectFlags = EObjectFlags::Transient;
    PActor* StateActor = World->SpawnActor(StateClass, StateParameters);
    PPlayerState* PlayerState = StateActor != nullptr
            && StateActor->IsA(PPlayerState::StaticClass())
        ? static_cast<PPlayerState*>(StateActor)
        : nullptr;
    if (PlayerState == nullptr
        || !Controller->SetPlayerState(PlayerState)
        || !CurrentGameState->AddPlayerState(PlayerState))
    {
        if (PlayerState != nullptr)
        {
            World->DestroyActor(PlayerState);
        }
        World->DestroyActor(Controller);
        return nullptr;
    }

    PlayerState->SetPlayerId(NextPlayerId++);
    NewPlayer->SetPlayerController(Controller);
    return Controller;
}

void PGameModeBase::PostLogin(PPlayerController*)
{
}

void PGameModeBase::DispatchPostLogin(PPlayerController* NewPlayer)
{
    PostLogin(NewPlayer);
    PostLoginEvent.Broadcast(NewPlayer);
}

bool PGameModeBase::HandleStartingNewPlayer(PPlayerController* NewPlayer)
{
    PWorld* World = GetWorld();
    PPlayer* Player = NewPlayer != nullptr ? NewPlayer->GetPlayer() : nullptr;
    const bool bIsLocalPlayer = Player != nullptr
        && Player->IsA(PLocalPlayer::StaticClass());
    if (World != nullptr && NewPlayer != nullptr && bIsLocalPlayer)
    {
        for (PLevel* Level : World->GetLevels())
        {
            if (Level == nullptr) continue;
            for (PActor* Actor : Level->GetActors())
            {
                PPawn* Pawn = Actor != nullptr && Actor->IsA(PPawn::StaticClass())
                    ? static_cast<PPawn*>(Actor) : nullptr;
                if (Pawn != nullptr
                    && !Pawn->IsPendingDestroy()
                    && Pawn->GetAutoPossessPlayerIndex() == 0
                    && Pawn->GetController() == nullptr)
                {
                    if (!NewPlayer->Possess(Pawn)) return false;
                    if (PPlayerState* State = NewPlayer->GetPlayerState())
                        State->SetIsSpectator(false);
                    return true;
                }
            }
        }
    }
    return RestartPlayer(NewPlayer);
}

bool PGameModeBase::RestartPlayer(PController* NewPlayer)
{
    if (NewPlayer == nullptr
        || NewPlayer->GetWorld() != GetWorld()
        || NewPlayer->IsPendingDestroy())
    {
        return false;
    }

    PPlayerState* PlayerState = NewPlayer->IsA(PPlayerController::StaticClass())
        ? static_cast<PPlayerController*>(NewPlayer)->GetPlayerState()
        : nullptr;
    if (PlayerState != nullptr)
    {
        PlayerState->SetIsSpectator(true);
    }

    if (PPawn* OldPawn = NewPlayer->GetPawn())
    {
        NewPlayer->UnPossess();
        OldPawn->Destroy();
    }

    PPlayerStart* StartSpot = ChoosePlayerStart(NewPlayer);
    if (StartSpot == nullptr)
    {
        PICO_LOG(
            LogEngine,
            Warning,
            "GameMode '{}' found no PlayerStart; spawning '{}' at the world origin",
            GetPathName(),
            NewPlayer->GetName().ToString());
    }
    PPawn* NewPawn = SpawnDefaultPawnFor(NewPlayer, StartSpot);
    if (NewPawn == nullptr || !NewPlayer->Possess(NewPawn))
    {
        if (NewPawn != nullptr)
        {
            NewPawn->Destroy();
        }
        return false;
    }
    if (PlayerState != nullptr)
    {
        PlayerState->SetIsSpectator(false);
    }
    if (StartSpot != nullptr)
    {
        StartSpot->NotifyPlayerSpawned(NewPawn);
    }
    return true;
}

void PGameModeBase::Logout(PController* Exiting)
{
    if (Exiting == nullptr || Exiting->GetWorld() != GetWorld())
    {
        return;
    }

    OnLogout(Exiting);
    if (Exiting->IsA(PPlayerController::StaticClass()))
    {
        LogoutEvent.Broadcast(static_cast<PPlayerController*>(Exiting));
    }
    PWorld* World = GetWorld();
    PPawn* Pawn = Exiting->GetPawn();
    Exiting->UnPossess();

    PPlayerState* PlayerState = nullptr;
    if (Exiting->IsA(PPlayerController::StaticClass()))
    {
        PPlayerController* PlayerController =
            static_cast<PPlayerController*>(Exiting);
        PlayerController->SetPlayer(nullptr);
        PlayerState = PlayerController->GetPlayerState();
        if (GameState.Get() != nullptr)
        {
            GameState->RemovePlayerState(PlayerState);
        }
        PlayerController->SetPlayerState(nullptr);
    }
    if (Pawn != nullptr)
    {
        World->DestroyActor(Pawn);
    }
    if (PlayerState != nullptr)
    {
        World->DestroyActor(PlayerState);
    }
    World->DestroyActor(Exiting);
}

void PGameModeBase::StartPlay()
{
    if (GetMatchState() == EMatchState::WaitingToStart
        && ReadyToStartMatch())
    {
        StartMatch();
    }
}

bool PGameModeBase::StartMatch()
{
    if (!SetMatchState(EMatchState::InProgress))
    {
        return false;
    }
    HandleMatchHasStarted();
    return true;
}

bool PGameModeBase::EndMatch()
{
    if (!SetMatchState(EMatchState::WaitingPostMatch))
    {
        return false;
    }
    HandleMatchHasEnded();
    return true;
}

bool PGameModeBase::AbortMatch()
{
    return SetMatchState(EMatchState::Aborted);
}

FOnGameModePlayerEvent& PGameModeBase::OnPostLoginEvent()
{
    return PostLoginEvent;
}

FOnGameModePlayerEvent& PGameModeBase::OnLogoutEvent()
{
    return LogoutEvent;
}

FOnGameModeMatchStateChanged& PGameModeBase::OnMatchStateChanged()
{
    return MatchStateChangedEvent;
}

bool PGameModeBase::ReadyToStartMatch() const
{
    return GetGameState() != nullptr
        && !GetGameState()->GetPlayerStates().empty();
}

void PGameModeBase::HandleMatchHasStarted()
{
}

void PGameModeBase::HandleMatchHasEnded()
{
}

bool PGameModeBase::SetMatchState(EMatchState NewState)
{
    PGameStateBase* State = GameState.Get();
    const EMatchState OldState = GetMatchState();
    if (State == nullptr || !CanTransitionTo(NewState)
        || !State->SetMatchState(NewState))
    {
        return false;
    }
    MatchStateChangedEvent.Broadcast(OldState, NewState);
    return true;
}

bool PGameModeBase::CanTransitionTo(EMatchState NewState) const
{
    const EMatchState Current = GetMatchState();
    if (Current == NewState)
    {
        return false;
    }
    if (NewState == EMatchState::LeavingMap)
    {
        return Current != EMatchState::LeavingMap;
    }
    if (NewState == EMatchState::Aborted)
    {
        return Current != EMatchState::LeavingMap
            && Current != EMatchState::Aborted;
    }
    return (Current == EMatchState::EnteringMap
            && NewState == EMatchState::WaitingToStart)
        || (Current == EMatchState::WaitingToStart
            && NewState == EMatchState::InProgress)
        || (Current == EMatchState::InProgress
            && NewState == EMatchState::WaitingPostMatch);
}

PPlayerStart* PGameModeBase::ChoosePlayerStart(PController* Player)
{
    const std::vector<PPlayerStart*> Starts = CollectPlayerStarts(GetWorld());
    if (Starts.empty()) return nullptr;
    const std::size_t Index = static_cast<std::size_t>(
        GetPlayerStartIndex(Player)) % Starts.size();
    return Starts[Index];
}

PPawn* PGameModeBase::SpawnDefaultPawnFor(
    PController* Controller,
    PPlayerStart* StartSpot)
{
    PWorld* World = GetWorld();
    const PClass* PawnClass = GetDefaultPawnClass();
    if (World == nullptr || Controller == nullptr || PawnClass == nullptr)
    {
        return nullptr;
    }
    FActorSpawnParameters Parameters;
    Parameters.Name = MakeUniqueActorName(*World, "DefaultPawn");
    Parameters.OverrideLevel = StartSpot != nullptr
        ? StartSpot->GetLevel()
        : World->GetCurrentLevel();
    Parameters.Owner = Controller;
    Parameters.ObjectFlags = EObjectFlags::Transient;
    PActor* Actor = World->SpawnActor(PawnClass, Parameters);
    PPawn* Pawn = Actor != nullptr && Actor->IsA(PPawn::StaticClass())
        ? static_cast<PPawn*>(Actor)
        : nullptr;
    if (Pawn != nullptr && StartSpot != nullptr)
    {
        FTransform SpawnTransform = StartSpot->GetActorTransform();
        constexpr float MinimumSpawnSeparation = 180.0f;
        const FVector3 OffsetDirection =
            SpawnTransform.Rotation.RotateVector(FVector3::RightVector);
        for (int32 Attempt = 0; Attempt < 16
            && IsPlayerSpawnOccupied(
                World, Pawn, SpawnTransform.Translation,
                MinimumSpawnSeparation); ++Attempt)
        {
            SpawnTransform.Translation +=
                OffsetDirection * MinimumSpawnSeparation;
        }
        Pawn->SetActorTransform(SpawnTransform);
    }
    return Pawn;
}

void PGameModeBase::OnLogout(PController*)
{
}

bool PGameModeBase::SetDefaultPawnClass(const PClass* Class)
{
    if (!CanEditClassDefaults() || Class == nullptr || !Class->IsChildOf(PPawn::StaticClass())) return false;
    DefaultPawnClass = Class;
    return true;
}

bool PGameModeBase::SetPlayerControllerClass(const PClass* Class)
{
    if (!CanEditClassDefaults() || Class == nullptr || !Class->IsChildOf(PPlayerController::StaticClass())) return false;
    PlayerControllerClass = Class;
    return true;
}

bool PGameModeBase::SetPlayerStateClass(const PClass* Class)
{
    if (!CanEditClassDefaults() || Class == nullptr || !Class->IsChildOf(PPlayerState::StaticClass())) return false;
    PlayerStateClass = Class;
    return true;
}

bool PGameModeBase::SetGameStateClass(const PClass* Class)
{
    if (!CanEditClassDefaults() || Class == nullptr || !Class->IsChildOf(PGameStateBase::StaticClass())) return false;
    GameStateClass = Class;
    return true;
}

PGameStateBase* PGameModeBase::CreateGameState()
{
    PWorld* World = GetWorld();
    const PClass* Class = GetGameStateClass();
    if (World == nullptr || Class == nullptr)
    {
        return nullptr;
    }
    FActorSpawnParameters Parameters;
    Parameters.Name = FName("GameState");
    Parameters.ObjectFlags = EObjectFlags::Transient;
    PActor* Actor = World->SpawnActor(Class, Parameters);
    GameState = Actor != nullptr && Actor->IsA(PGameStateBase::StaticClass())
        ? static_cast<PGameStateBase*>(Actor)
        : nullptr;
    if (GameState.Get() != nullptr)
    {
        SetMatchState(EMatchState::WaitingToStart);
    }
    return GameState.Get();
}

void PGameModeBase::BeginDestroy()
{
    if (GameState.Get() != nullptr)
    {
        SetMatchState(EMatchState::LeavingMap);
    }
    PostLoginEvent.Clear();
    LogoutEvent.Clear();
    MatchStateChangedEvent.Clear();
    GameState.Reset();
    PActor::BeginDestroy();
}
}
