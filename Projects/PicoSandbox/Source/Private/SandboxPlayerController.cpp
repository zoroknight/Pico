#include "PicoSandbox/SandboxPlayerController.h"

#include "Pico/Asset/ThirdPersonControlProfile.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "PicoSandbox/SandboxPawn.h"
#include "PicoSandbox/SandboxReplicationLabActor.h"

#include <array>

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

PSandboxPlayerController::PSandboxPlayerController(
    const Pico::FObjectConstructionParams& Params)
    : PPlayerController(Params)
{
    SetViewPitchLimits(-75.0f, 55.0f);
}

void PSandboxPlayerController::OnPossess(Pico::PPawn* InPawn)
{
    PPlayerController::OnPossess(InPawn);
    PSandboxPawn* SandboxPawn = InPawn != nullptr
            && InPawn->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(InPawn) : nullptr;
    if (SandboxPawn != nullptr)
    {
        SandboxPawn->LoadAndApplyThirdPersonControlProfile();
        const Pico::FThirdPersonControlProfileData& Profile =
            SandboxPawn->GetActiveControlProfile();
        SetViewPitchLimits(Profile.MinimumCameraPitch, Profile.MaximumCameraPitch);
        SetControlRotation({Profile.InitialCameraPitch,
            InPawn->GetActorRotation().Yaw, 0.0f});
    }
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
    const Pico::FThirdPersonControlProfileData& Profile =
        SandboxPawn->GetActiveControlProfile();
    if (Movement != nullptr)
    {
        Movement->SetOrientRotationToMovement(
            ControlMode == ECharacterControlMode::FreeLook
                && Profile.bOrientRotationToMovement
                && SandboxPawn->GetMovementReference() != EMovementReference::ActorRotation);
        Movement->SetUseControllerDesiredRotation(
            ControlMode == ECharacterControlMode::Strafe
                && Profile.bUseControllerDesiredRotationWhenAiming);
    }
    SandboxPawn->SetUseControllerRotationYaw(Profile.bUseControllerRotationYaw);
    Pico::PObject* BoomObject = Pico::FindObject(SandboxPawn, Pico::FName("CameraBoom"));
    auto* Boom = BoomObject != nullptr
            && BoomObject->IsA(Pico::PSpringArmComponent::StaticClass())
        ? static_cast<Pico::PSpringArmComponent*>(BoomObject) : nullptr;
    if (Boom != nullptr)
    {
        Boom->SetTargetArmLength(
            ControlMode == ECharacterControlMode::Strafe
                ? Profile.AimCameraArmLength : Profile.DefaultCameraArmLength);
        Boom->SetSocketOffset(
            ControlMode == ECharacterControlMode::Strafe
                ? Profile.AimCameraSocketOffset
                : Pico::FVector3::ZeroVector);
    }
}

void PSandboxPlayerController::DoMove(float Right, float Forward)
{
    PSandboxPawn* ControlledPawn = GetPawn() != nullptr
            && GetPawn()->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(GetPawn()) : nullptr;
    if (ControlledPawn == nullptr) return;

    const EMovementReference Reference = ControlMode == ECharacterControlMode::Strafe
        ? EMovementReference::ControlRotation
        : ControlledPawn->GetMovementReference();
    const Pico::FThirdPersonMovementBasis Basis =
        Pico::BuildThirdPersonMovementBasis(
            Reference,
            GetControlRotation().Yaw,
            ControlledPawn->GetActorRotation().Yaw);
    ControlledPawn->AddMovementInput(Basis.Forward, Forward);
    ControlledPawn->AddMovementInput(Basis.ScreenRight, Right);
}

void PSandboxPlayerController::Tick(float DeltaSeconds)
{
    PPlayerController::Tick(DeltaSeconds);
    Pico::PPlayer* OwningPlayer = GetPlayer();
    if (OwningPlayer == nullptr
        || !OwningPlayer->IsA(Pico::PLocalPlayer::StaticClass()))
    {
        return;
    }
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
    if (Input.WasKeyPressed(Pico::EKey::F))
    {
        Pico::PWorld* World = GetWorld();
        PSandboxReplicationLabActor* Door = nullptr;
        if (World != nullptr)
        {
            for (Pico::PLevel* Level : World->GetLevels())
            {
                if (Level == nullptr) continue;
                for (Pico::PActor* Actor : Level->GetActors())
                {
                    if (Actor != nullptr
                        && Actor->IsA(PSandboxReplicationLabActor::StaticClass())
                        && !Actor->IsPendingDestroy())
                    {
                        Door = static_cast<PSandboxReplicationLabActor*>(Actor);
                        break;
                    }
                }
                if (Door != nullptr) break;
            }
        }
        if (Door != nullptr)
        {
            const std::array<Pico::FFunctionValue, 1> Arguments = {
                static_cast<Pico::PObject*>(Door)
            };
            GameEngine->GetNetDriver().CallRemoteFunction(
                this, Pico::FName("ServerTryInteract"), Arguments);
        }
    }
}

void PSandboxPlayerController::ServerTryInteract(Pico::PActor* Target)
{
    PSandboxReplicationLabActor* Door = Target != nullptr
            && Target->IsA(PSandboxReplicationLabActor::StaticClass())
        ? static_cast<PSandboxReplicationLabActor*>(Target) : nullptr;
    Pico::PPawn* ControlledPawn = GetPawn();
    Pico::PPlayer* OwningPlayer = GetPlayer();
    Pico::PGameInstance* GameInstance =
        OwningPlayer != nullptr ? OwningPlayer->GetGameInstance() : nullptr;
    Pico::FGameEngine* GameEngine =
        GameInstance != nullptr ? GameInstance->GetGameEngine() : nullptr;
    const bool bAccepted = Door != nullptr
        && ControlledPawn != nullptr
        && (ControlledPawn->GetActorLocation() - Door->GetActorLocation()).Size()
            <= 500.0f
        && Door->ToggleDoor();
    if (GameEngine == nullptr) return;

    const std::array<Pico::FFunctionValue, 1> ResultArguments = {bAccepted};
    GameEngine->GetNetDriver().CallRemoteFunction(
        this, Pico::FName("ClientInteractionResult"), ResultArguments);
    if (bAccepted)
    {
        const std::array<Pico::FFunctionValue, 1> PulseArguments = {
            Door->GetDoorUseCount()
        };
        GameEngine->GetNetDriver().CallRemoteFunction(
            Door, Pico::FName("MulticastDoorPulse"), PulseArguments);
    }
}

void PSandboxPlayerController::ClientInteractionResult(bool bAccepted)
{
    bLastInteractionAccepted = bAccepted;
    ++ClientInteractionResultCount;
}
}
