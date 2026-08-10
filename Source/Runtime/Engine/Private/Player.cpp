#include "Pico/Engine/Player.h"

#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/PlayerController.h"

namespace Pico
{
PICO_DEFINE_CLASS(PPlayer)

bool PPlayer::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, PlayerController, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PPlayer::PPlayer(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

PPlayerController* PPlayer::GetPlayerController() const
{
    return PlayerController.Get();
}

PGameInstance* PPlayer::GetGameInstance() const
{
    PObject* Outer = GetOuter();
    return Outer != nullptr && Outer->IsA(PGameInstance::StaticClass())
        ? static_cast<PGameInstance*>(Outer)
        : nullptr;
}

void PPlayer::SetPlayerController(PPlayerController* InPlayerController)
{
    PPlayerController* OldController = PlayerController.Get();
    if (OldController == InPlayerController)
    {
        return;
    }
    if (OldController != nullptr && OldController->GetPlayer() == this)
    {
        OldController->Player.Reset();
    }
    PlayerController = InPlayerController;
    if (InPlayerController != nullptr)
    {
        if (PPlayer* OldPlayer = InPlayerController->GetPlayer())
        {
            OldPlayer->PlayerController.Reset();
        }
        InPlayerController->Player = this;
    }
}

void PPlayer::BeginDestroy()
{
    SetPlayerController(nullptr);
    PObject::BeginDestroy();
}
}
