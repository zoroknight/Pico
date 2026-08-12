#include "PicoSandbox/SandboxPlayerController.h"

#include "Pico/Core/Math/Quat.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "PicoSandbox/SandboxPawn.h"

namespace PicoSandbox
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxPlayerController)

PSandboxPlayerController::PSandboxPlayerController(
    const Pico::FObjectConstructionParams& Params)
    : PPlayerController(Params)
{
}

void PSandboxPlayerController::OnPossess(Pico::PPawn* Pawn)
{
    PPlayerController::OnPossess(Pawn);
    if (Pawn != nullptr)
        SetControlRotation({-15.0f, Pawn->GetActorRotation().Yaw, 0.0f});
}

void PSandboxPlayerController::Tick(float DeltaSeconds)
{
    PPlayerController::Tick(DeltaSeconds);
    Pico::PPlayer* OwningPlayer = GetPlayer();
    Pico::PGameInstance* GameInstance =
        OwningPlayer != nullptr ? OwningPlayer->GetGameInstance() : nullptr;
    Pico::FGameEngine* GameEngine =
        GameInstance != nullptr ? GameInstance->GetGameEngine() : nullptr;
    if (GameEngine == nullptr)
    {
        return;
    }

    Pico::FInputSystem& Input = GameEngine->GetInputSystem();
    AddYawInput(Input.GetAxisValue("LookYaw"));
    AddPitchInput(Input.GetAxisValue("LookPitch"));
    PSandboxPawn* ControlledPawn = GetPawn() != nullptr
            && GetPawn()->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(GetPawn())
        : nullptr;
    if (ControlledPawn != nullptr)
    {
        const Pico::FQuat ViewRotation = Pico::FQuat::FromRotator(
            {0.0f, GetControlRotation().Yaw, 0.0f});
        Pico::FVector3 Forward = ViewRotation.RotateVector(Pico::FVector3::ForwardVector)
            .GetSafeNormal();
        Pico::FVector3 Right = Pico::FVector3::Cross(
            Pico::FVector3::UpVector, Forward).GetSafeNormal();
        const Pico::FVector3 Direction =
            Forward * Input.GetAxisValue("MoveForward")
            + Right * Input.GetAxisValue("MoveRight");
        ControlledPawn->AddMovementInput(Direction);
        if (Input.WasActionPressed("Jump"))
        {
            ControlledPawn->Jump();
        }
        if (Input.WasActionReleased("Jump"))
        {
            ControlledPawn->StopJumping();
        }
    }
    if (Input.WasKeyPressed(Pico::EKey::R))
    {
        Pico::PWorld* World = GetWorld();
        if (World != nullptr && World->GetGameMode() != nullptr)
        {
            World->GetGameMode()->RestartPlayer(this);
        }
    }
}
}
