#pragma once

#include "Pico/Engine/PlayerController.h"

namespace PicoSandbox
{
enum class ECharacterControlMode : Pico::uint8
{
    FreeLook,
    Strafe
};

const char* ToString(ECharacterControlMode Mode);

class PSandboxPlayerController final : public Pico::PPlayerController
{
    PICO_DECLARE_CLASS(PSandboxPlayerController, Pico::PPlayerController)

public:
    void Tick(float DeltaSeconds) override;
    ECharacterControlMode GetControlMode() const;
    void SetControlMode(ECharacterControlMode Mode);

protected:
    explicit PSandboxPlayerController(
        const Pico::FObjectConstructionParams& Params);
    void OnPossess(Pico::PPawn* Pawn) override;

private:
    void ApplyControlMode();
    void DoMove(float Right, float Forward);
    ECharacterControlMode ControlMode = ECharacterControlMode::FreeLook;
};
}
