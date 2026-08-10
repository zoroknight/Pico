#include "Pico/Engine/PlayerController.h"

#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/Player.h"

namespace Pico
{
PICO_DEFINE_CLASS(PPlayerController)

bool PPlayerController::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::Replicated;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, PlayerState, Metadata);
    Metadata.Flags = EPropertyFlags::Transient;
    PICO_ADD_PROPERTY_METADATA(Properties, Player, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PPlayerController::PPlayerController(const FObjectConstructionParams& Params)
    : PController(Params)
{
    PrimaryActorTick.SetTickGroup(ETickGroup::PrePhysics);
}

PPlayerState* PPlayerController::GetPlayerState() const
{
    return PlayerState.Get();
}

PPlayer* PPlayerController::GetPlayer() const
{
    return Player.Get();
}

bool PPlayerController::SetPlayerState(PPlayerState* InPlayerState)
{
    if (InPlayerState != nullptr && InPlayerState->GetWorld() != GetWorld())
    {
        return false;
    }
    PlayerState = InPlayerState;
    return true;
}

void PPlayerController::SetPlayer(PPlayer* InPlayer)
{
    if (InPlayer != nullptr)
    {
        InPlayer->SetPlayerController(this);
    }
    else if (PPlayer* OldPlayer = Player.Get())
    {
        OldPlayer->SetPlayerController(nullptr);
    }
}

void PPlayerController::OnPossess(PPawn*)
{
    if (PPlayerState* State = PlayerState.Get())
    {
        State->SetIsSpectator(false);
    }
}

void PPlayerController::OnUnPossess(PPawn*)
{
    if (PPlayerState* State = PlayerState.Get())
    {
        State->SetIsSpectator(true);
    }
}

void PPlayerController::BeginDestroy()
{
    SetPlayer(nullptr);
    PlayerState.Reset();
    PController::BeginDestroy();
}
}
