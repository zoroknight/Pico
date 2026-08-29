#include "Pico/Engine/PlayerState.h"

namespace Pico
{
PICO_DEFINE_CLASS(PPlayerState)

bool PPlayerState::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::Replicated;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, PlayerId, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, Score, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, bIsSpectator, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PPlayerState::PPlayerState(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

int32 PPlayerState::GetPlayerId() const { return PlayerId; }
void PPlayerState::SetPlayerId(int32 InPlayerId)
{
    if (PlayerId == InPlayerId) return;
    PlayerId = InPlayerId;
    MarkReplicatedPropertyDirty(FName("PlayerId"));
}
float PPlayerState::GetScore() const { return Score; }
void PPlayerState::SetScore(float InScore)
{
    if (Score == InScore) return;
    Score = InScore;
    MarkReplicatedPropertyDirty(FName("Score"));
}
bool PPlayerState::IsSpectator() const { return bIsSpectator; }
void PPlayerState::SetIsSpectator(bool bValue)
{
    if (bIsSpectator == bValue) return;
    bIsSpectator = bValue;
    MarkReplicatedPropertyDirty(FName("bIsSpectator"));
}
}
