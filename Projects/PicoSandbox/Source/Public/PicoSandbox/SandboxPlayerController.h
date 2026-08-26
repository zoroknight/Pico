#pragma once

#include "Pico/Engine/PlayerController.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPlayerController.generated.h"

namespace PicoSandbox
{
class PSandboxPawn;

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
    Pico::int32 GetGameplayAbilityResultCount() const { return GameplayAbilityResultCount; }
    bool WasLastGameplayAbilityAccepted() const { return bLastGameplayAbilityAccepted; }
    Pico::int32 GetLastGameplayAbilityId() const { return LastGameplayAbilityId; }

protected:
    explicit PSandboxPlayerController(
        const Pico::FObjectConstructionParams& Params);
    void OnPossess(Pico::PPawn* Pawn) override;

private:
    void ApplyControlMode();
    void DoMove(float Right, float Forward);
    PSandboxPawn* FindNearestOtherSandboxPawn(float MaxDistance) const;

    PFUNCTION(Server, Reliable)
    void ServerTryInteract(Pico::PActor* Target);

    PFUNCTION(Client, Reliable)
    void ClientInteractionResult(bool bAccepted);

    PFUNCTION(Server, Reliable)
    void ServerActivateGravityShot();

    PFUNCTION(Server, Reliable)
    void ServerActivateBurnShot();

    PFUNCTION(Server, Reliable)
    void ServerActivateFreezeShot();

    PFUNCTION(Client, Reliable)
    void ClientGameplayAbilityResult(
        Pico::int32 AbilityId,
        Pico::int32 PredictionKey,
        bool bAccepted,
        Pico::FVector3 AuthorityLocation,
        float AuthorityMana);

    ECharacterControlMode ControlMode = ECharacterControlMode::FreeLook;
    Pico::int32 ClientInteractionResultCount = 0;
    bool bLastInteractionAccepted = false;
    Pico::int32 GameplayAbilityResultCount = 0;
    Pico::int32 LastGameplayAbilityId = 0;
    bool bLastGameplayAbilityAccepted = false;
};
}
