#pragma once

#include "Pico/Engine/GameInstance.h"
namespace PicoSandbox
{
class PSandboxGameInstance final : public Pico::PGameInstance
{
    PICO_DECLARE_CLASS(PSandboxGameInstance, Pico::PGameInstance)

public:
    bool Init(Pico::FGameEngine& GameEngine) override;

private:
    explicit PSandboxGameInstance(const Pico::FObjectConstructionParams& Params);
};
}
