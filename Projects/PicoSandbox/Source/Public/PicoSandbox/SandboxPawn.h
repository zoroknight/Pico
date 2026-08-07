#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ReflectionMacros.h"

namespace Pico
{
class FInputSystem;
}

namespace PicoSandbox
{
class PSandboxPawn final : public Pico::PActor
{
    PICO_DECLARE_CLASS(PSandboxPawn, Pico::PActor)

public:
    void SetInputSystem(Pico::FInputSystem* InInputSystem);
    float GetMoveSpeed() const;
    void Tick(float DeltaSeconds) override;

protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);

private:
    Pico::FInputSystem* InputSystem = nullptr;
    float MoveSpeed = 250.0f;
};
}
