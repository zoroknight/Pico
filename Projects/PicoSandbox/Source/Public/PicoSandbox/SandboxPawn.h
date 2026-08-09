#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPawn.generated.h"

namespace Pico
{
class FInputSystem;
}

namespace PicoSandbox
{
PCLASS()
class PSandboxPawn final : public Pico::PActor
{
    GENERATED_BODY()

public:
    void SetInputSystem(Pico::FInputSystem* InInputSystem);
    float GetMoveSpeed() const;
    void Tick(float DeltaSeconds) override;

protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;

private:
    Pico::FInputSystem* InputSystem = nullptr;
    PPROPERTY()
    float MoveSpeed = 250.0f;
};
}
