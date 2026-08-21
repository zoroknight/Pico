#pragma once

#include "Pico/Engine/PlayerController.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPlayerController.generated.h"

namespace PicoSandbox
{
enum class ECharacterControlMode : Pico::uint8
{
    FreeLook,
    Strafe
};

const char* ToString(ECharacterControlMode Mode);

PCLASS()
class PSandboxPlayerController final : public Pico::PPlayerController
{
    GENERATED_BODY()

public:
    void Tick(float DeltaSeconds) override;
    ECharacterControlMode GetControlMode() const;
    void SetControlMode(ECharacterControlMode Mode);
    Pico::int32 GetClientInteractionResultCount() const
    {
        return ClientInteractionResultCount;
    }
    bool WasLastInteractionAccepted() const
    {
        return bLastInteractionAccepted;
    }

protected:
    explicit PSandboxPlayerController(
        const Pico::FObjectConstructionParams& Params);
    void OnPossess(Pico::PPawn* Pawn) override;

private:
    void ApplyControlMode();
    void DoMove(float Right, float Forward);

    PFUNCTION(Server, Reliable)
    void ServerTryInteract(Pico::PActor* Target);

    PFUNCTION(Client, Reliable)
    void ClientInteractionResult(bool bAccepted);

    ECharacterControlMode ControlMode = ECharacterControlMode::FreeLook;
    Pico::int32 ClientInteractionResultCount = 0;
    bool bLastInteractionAccepted = false;
};
}
