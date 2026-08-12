#pragma once

#include "Pico/Engine/PlayerController.h"

namespace PicoSandbox
{
class PSandboxPlayerController final : public Pico::PPlayerController
{
    PICO_DECLARE_CLASS(PSandboxPlayerController, Pico::PPlayerController)

public:
    void Tick(float DeltaSeconds) override;

protected:
    explicit PSandboxPlayerController(
        const Pico::FObjectConstructionParams& Params);
    void OnPossess(Pico::PPawn* Pawn) override;
};
}
