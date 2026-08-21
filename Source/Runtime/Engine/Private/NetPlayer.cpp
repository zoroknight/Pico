#include "Pico/Engine/NetPlayer.h"

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PNetPlayer)

PNetPlayer::PNetPlayer(const FObjectConstructionParams& Params)
    : PPlayer(Params)
{
}

FNetConnectionId PNetPlayer::GetConnectionId() const
{
    return ConnectionId;
}

void PNetPlayer::SetConnectionId(FNetConnectionId InConnectionId)
{
    ConnectionId = InConnectionId;
}
}
