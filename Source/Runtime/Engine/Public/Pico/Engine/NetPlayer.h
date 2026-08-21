#pragma once

#include "Pico/Engine/Player.h"
#include "Pico/Net/NetTypes.h"

namespace Pico
{
class PNetPlayer final : public PPlayer
{
    PICO_DECLARE_CLASS(PNetPlayer, PPlayer)

public:
    FNetConnectionId GetConnectionId() const;

protected:
    explicit PNetPlayer(const FObjectConstructionParams& Params);

private:
    friend class PGameInstance;
    void SetConnectionId(FNetConnectionId InConnectionId);
    FNetConnectionId ConnectionId;
};
}
