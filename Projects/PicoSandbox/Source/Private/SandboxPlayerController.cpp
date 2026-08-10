#include "PicoSandbox/SandboxPlayerController.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "PicoSandbox/SandboxPawn.h"

namespace PicoSandbox
{
namespace
{
Pico::PCameraComponent* FindActiveCamera(Pico::PWorld* World)
{
    if (World == nullptr)
    {
        return nullptr;
    }
    Pico::PCameraComponent* FallbackCamera = nullptr;
    for (Pico::PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (Pico::PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr)
            {
                continue;
            }
            for (Pico::PActorComponent* Component : Actor->GetComponents())
            {
                if (Component != nullptr
                    && Component->IsA(Pico::PCameraComponent::StaticClass()))
                {
                    auto* Camera = static_cast<Pico::PCameraComponent*>(Component);
                    if (Camera->IsActive())
                    {
                        return Camera;
                    }
                    if (FallbackCamera == nullptr)
                    {
                        FallbackCamera = Camera;
                    }
                }
            }
        }
    }
    return FallbackCamera;
}
}

PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxPlayerController)

PSandboxPlayerController::PSandboxPlayerController(
    const Pico::FObjectConstructionParams& Params)
    : PPlayerController(Params)
{
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
    PSandboxPawn* ControlledPawn = GetPawn() != nullptr
            && GetPawn()->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(GetPawn())
        : nullptr;
    if (ControlledPawn != nullptr)
    {
        Pico::FVector3 Forward = Pico::FVector3(-500.0f, 500.0f, 0.0f)
            .GetSafeNormal();
        Pico::FVector3 Right = Pico::FVector3::Cross(
            Pico::FVector3::UpVector, Forward).GetSafeNormal();
        if (Pico::PCameraComponent* Camera = FindActiveCamera(GetWorld()))
        {
            const Pico::FVector3 ViewForward = Camera->GetViewForward();
            const Pico::FVector3 FlatForward(
                ViewForward.X, ViewForward.Y, 0.0f);
            if (!FlatForward.IsNearlyZero())
            {
                Forward = FlatForward.GetSafeNormal();
                Right = Pico::FVector3::Cross(
                    Pico::FVector3::UpVector, Forward).GetSafeNormal();
            }
        }
        const Pico::FVector3 Direction =
            Forward * Input.GetAxisValue("MoveForward")
            + Right * Input.GetAxisValue("MoveRight");
        ControlledPawn->MoveInWorldDirection(Direction, DeltaSeconds);
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
