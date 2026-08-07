#include "PicoSandbox/SandboxPawn.h"

#include "Pico/Input/InputSystem.h"

#include <utility>
#include <vector>

namespace PicoSandbox
{
PICO_DEFINE_CLASS(PSandboxPawn)

bool PSandboxPawn::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, MoveSpeed);
    return Class.AddProperties(std::move(Properties));
}

PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PActor(Params)
{
}

void PSandboxPawn::SetInputSystem(Pico::FInputSystem* InInputSystem)
{
    InputSystem = InInputSystem;
}

float PSandboxPawn::GetMoveSpeed() const
{
    return MoveSpeed;
}

void PSandboxPawn::Tick(float DeltaSeconds)
{
    if (InputSystem == nullptr)
    {
        return;
    }

    const float Forward = InputSystem->GetAxisValue("MoveForward");
    const float Right = InputSystem->GetAxisValue("MoveRight");
    const Pico::FVector3 Direction(Forward, Right, 0.0f);
    if (!Direction.IsNearlyZero())
    {
        SetActorLocation(
            GetActorLocation()
                + Direction.GetSafeNormal() * MoveSpeed * DeltaSeconds);
    }
}
}
