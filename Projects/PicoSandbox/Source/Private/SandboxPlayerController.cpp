#include "PicoSandbox/SandboxPlayerController.h"

#include "Pico/Core/Math/Quat.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "PicoSandbox/SandboxPawn.h"

namespace PicoSandbox
{
const char* ToString(ECharacterControlMode Mode)
{
    switch (Mode)
    {
    case ECharacterControlMode::FreeLook: return "FreeLook";
    case ECharacterControlMode::Strafe: return "Strafe";
    }
    return "Unknown";
}

PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxPlayerController)

PSandboxPlayerController::PSandboxPlayerController(
    const Pico::FObjectConstructionParams& Params)
    : PPlayerController(Params)
{
}

void PSandboxPlayerController::OnPossess(Pico::PPawn* InPawn)
{
    PPlayerController::OnPossess(InPawn);
    if (InPawn != nullptr)
        SetControlRotation({-15.0f, InPawn->GetActorRotation().Yaw, 0.0f});
    ApplyControlMode();
}

ECharacterControlMode PSandboxPlayerController::GetControlMode() const
{
    return ControlMode;
}

void PSandboxPlayerController::SetControlMode(ECharacterControlMode Mode)
{
    ControlMode = Mode;
    ApplyControlMode();
}

void PSandboxPlayerController::ApplyControlMode()
{
    PSandboxPawn* SandboxPawn = GetPawn() != nullptr
            && GetPawn()->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(GetPawn()) : nullptr;
    if (SandboxPawn == nullptr) return;
    Pico::PCharacterMovementComponent* Movement = SandboxPawn->GetCharacterMovement();
    if (Movement != nullptr)
    {
        Movement->SetOrientRotationToMovement(
            ControlMode == ECharacterControlMode::FreeLook
                && SandboxPawn->GetMovementReference() != EMovementReference::ActorRotation);
        Movement->SetUseControllerDesiredRotation(
            ControlMode == ECharacterControlMode::Strafe);
    }
    SandboxPawn->SetUseControllerRotationYaw(false);
    Pico::PObject* BoomObject = Pico::FindObject(SandboxPawn, Pico::FName("CameraBoom"));
    auto* Boom = BoomObject != nullptr
            && BoomObject->IsA(Pico::PSpringArmComponent::StaticClass())
        ? static_cast<Pico::PSpringArmComponent*>(BoomObject) : nullptr;
    if (Boom != nullptr)
    {
        Boom->SetTargetArmLength(
            ControlMode == ECharacterControlMode::Strafe ? 340.0f : 420.0f);
        Boom->SetSocketOffset(
            ControlMode == ECharacterControlMode::Strafe
                ? Pico::FVector3(0.0f, 65.0f, 10.0f)
                : Pico::FVector3::ZeroVector);
    }
}

void PSandboxPlayerController::DoMove(float Right, float Forward)
{
    PSandboxPawn* ControlledPawn = GetPawn() != nullptr
            && GetPawn()->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(GetPawn()) : nullptr;
    if (ControlledPawn == nullptr) return;

    float BasisYaw = 0.0f;
    const EMovementReference Reference = ControlMode == ECharacterControlMode::Strafe
        ? EMovementReference::ControlRotation
        : ControlledPawn->GetMovementReference();
    if (Reference == EMovementReference::ControlRotation)
        BasisYaw = GetControlRotation().Yaw;
    else if (Reference == EMovementReference::ActorRotation)
        BasisYaw = ControlledPawn->GetActorRotation().Yaw;

    const Pico::FQuat YawRotation = Pico::FQuat::FromRotator(
        {0.0f, BasisYaw, 0.0f});
    const Pico::FVector3 ForwardDirection = YawRotation.RotateVector(
        Pico::FVector3::ForwardVector).GetSafeNormal();
    const Pico::FVector3 RightDirection = Pico::FVector3::Cross(
        ForwardDirection, Pico::FVector3::UpVector).GetSafeNormal();

    ControlledPawn->AddMovementInput(ForwardDirection, Forward);
    ControlledPawn->AddMovementInput(RightDirection, Right);
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
    SetControlMode(Input.IsActionDown("Aim")
        ? ECharacterControlMode::Strafe
        : ECharacterControlMode::FreeLook);
    AddYawInput(Input.GetAxisValue("LookYaw"));
    AddPitchInput(Input.GetAxisValue("LookPitch"));
    PSandboxPawn* ControlledPawn = GetPawn() != nullptr
            && GetPawn()->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(GetPawn())
        : nullptr;
    if (ControlledPawn != nullptr)
    {
        DoMove(
            Input.GetAxisValue("MoveRight"),
            Input.GetAxisValue("MoveForward"));
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
