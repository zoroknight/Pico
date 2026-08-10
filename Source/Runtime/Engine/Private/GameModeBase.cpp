#include "Pico/Engine/GameModeBase.h"

#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Core/Log.h"
#include "Pico/Object/ObjectGlobals.h"

#include <string>

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

bool PGameModeBase::HandleStartingNewPlayer(PPlayerController* NewPlayer)
{
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
    return true;
}

void PGameModeBase::Logout(PController* Exiting)
{
    if (Exiting == nullptr || Exiting->GetWorld() != GetWorld())
    {
        return;
    }

    OnLogout(Exiting);
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

PPlayerStart* PGameModeBase::ChoosePlayerStart(PController*)
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        return nullptr;
    }
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor != nullptr
                && !Actor->IsPendingDestroy()
                && Actor->IsA(PPlayerStart::StaticClass()))
            {
                return static_cast<PPlayerStart*>(Actor);
            }
        }
    }
    return nullptr;
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
        Pawn->SetActorTransform(StartSpot->GetActorTransform());
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
    return GameState.Get();
}

void PGameModeBase::BeginDestroy()
{
    GameState.Reset();
    PActor::BeginDestroy();
}
}
