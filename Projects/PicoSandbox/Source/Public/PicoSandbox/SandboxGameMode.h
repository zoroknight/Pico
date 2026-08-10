#pragma once

#include "Pico/Engine/GameModeBase.h"

namespace PicoSandbox
{
class PSandboxGameMode final : public Pico::PGameModeBase
{
    PICO_DECLARE_CLASS(PSandboxGameMode, Pico::PGameModeBase)

protected:
    explicit PSandboxGameMode(const Pico::FObjectConstructionParams& Params);
};
}
