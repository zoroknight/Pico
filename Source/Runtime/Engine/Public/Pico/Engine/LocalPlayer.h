#pragma once

#include "Pico/Engine/Player.h"
#include "Pico/Object/ReflectionMacros.h"

namespace Pico
{
class PPlayerController;

class PLocalPlayer : public PPlayer
{
    PICO_DECLARE_CLASS(PLocalPlayer, PPlayer)

public:
    int32 GetLocalPlayerIndex() const;
protected:
    explicit PLocalPlayer(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

private:
    friend class PGameInstance;
    void SetLocalPlayerIndex(int32 InIndex);
    int32 LocalPlayerIndex = 0;
};
}
