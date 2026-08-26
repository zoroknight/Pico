#pragma once

#include "Pico/Asset/ThirdPersonControlProfile.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/Character.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/GameplayAbilityPrediction.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPawn.generated.h"

#include <string>

namespace PicoSandbox
{
enum class ESandboxProjectileEffect : Pico::int32;
using EMovementReference = Pico::EThirdPersonMovementReference;

const char* ToString(EMovementReference Reference);

PCLASS()
class PSandboxPawn final : public Pico::PCharacter
{
    GENERATED_BODY()

public:
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void EndPlay() override;
    EMovementReference GetMovementReference() const;
    void SetMovementReference(EMovementReference Reference);
    const Pico::FAssetPath& GetThirdPersonControlProfileAsset() const;
    void SetThirdPersonControlProfileAsset(const Pico::FAssetPath& AssetPath);
    bool LoadAndApplyThirdPersonControlProfile();
    const Pico::FThirdPersonControlProfileData& GetActiveControlProfile() const;
    Pico::uint64 GetActiveControlProfileHash() const;
    bool HasLoadedControlProfile() const;
    Pico::PGameplayAbilitySystemComponent* GetAbilitySystemComponent() const;
    bool IsStunned() const;
    bool IsFrozen() const;
    bool IsBurning() const;
    bool IsGravityAffected() const;
    float GetReplicatedHealth() const;
    float GetReplicatedMana() const;
    float GetReplicatedBurnRemaining() const;
    float GetReplicatedGravityRemaining() const;
    float GetReplicatedFreezeRemaining() const;
    float GetReplicatedGravityCooldownRemaining() const;
    float GetReplicatedBurnCooldownRemaining() const;
    float GetReplicatedFreezeCooldownRemaining() const;
    Pico::int32 GetAbilityLoadoutBits() const;
    bool SetAbilityLoadoutBits(Pico::int32 Bits);
    bool TryAuthorityGravityShot(PSandboxPawn* Target);
    bool TryAuthorityBurnShot(PSandboxPawn* Target);
    bool TryAuthorityFreezeShot(PSandboxPawn* Target);
    bool ApplyAuthorityProjectileEffect(
        PSandboxPawn* Source,
        ESandboxProjectileEffect Effect);
    Pico::FGameplayPredictionKey BeginPredictedDash(const Pico::FVector3& Direction);
    bool ResolvePredictedDash(
        Pico::FGameplayPredictionKey Key,
        bool bAccepted,
        const Pico::FVector3& AuthorityLocation,
        float AuthorityMana);
protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;
    void PostLoad() override;

private:
    void SanitizeAbilityProfile();
    Pico::int32 BuildAbilityLoadoutBits() const;
    void ApplyAbilitySpecOverrides();
    bool ActivateImmediateAbility(const Pico::PClass* AbilityClass);
    bool SpawnAuthorityProjectile(
        PSandboxPawn* Target,
        const Pico::PClass* AbilityClass,
        ESandboxProjectileEffect Effect,
        float MaxRange);
    void RefreshReplicatedGameplayState();
    void ApplyReplicatedGameplayState();

    PFUNCTION()
    void OnRep_GameplayState();

    PPROPERTY()
    Pico::int32 MovementReferenceValue =
        static_cast<Pico::int32>(EMovementReference::ControlRotation);

    PPROPERTY(Asset=ThirdPersonControlProfile)
    Pico::FAssetPath ThirdPersonControlProfileAsset;

    PPROPERTY(ReadOnly)
    Pico::int32 AbilityLoadoutBits = 7;

    // Mini GAS Profile: reflected instance defaults that are copied into runtime specs.
    PPROPERTY()
    float InitialHealth = 100.0f;

    PPROPERTY()
    float InitialMana = 100.0f;

    PPROPERTY()
    bool bGravityShotEnabled = true;

    PPROPERTY()
    float GravityManaCost = 20.0f;

    PPROPERTY()
    float GravityCooldownSeconds = 1.5f;

    PPROPERTY()
    float GravityRange = 900.0f;

    PPROPERTY()
    float GravityProjectileSpeed = 650.0f;

    PPROPERTY()
    float GravityEffectDuration = 2.5f;

    PPROPERTY()
    float GravityLaunchVelocity = 260.0f;

    PPROPERTY()
    Pico::FVector3 GravityProjectileColor = Pico::FVector3(0.70f, 0.18f, 1.0f);

    PPROPERTY()
    bool bBurnShotEnabled = true;

    PPROPERTY()
    float BurnManaCost = 15.0f;

    PPROPERTY()
    float BurnCooldownSeconds = 1.0f;

    PPROPERTY()
    float BurnRange = 900.0f;

    PPROPERTY()
    float BurnProjectileSpeed = 650.0f;

    PPROPERTY()
    float BurnEffectDuration = 8.0f;

    PPROPERTY()
    float BurnTickInterval = 1.0f;

    PPROPERTY()
    float BurnDamagePerTick = 5.0f;

    PPROPERTY()
    Pico::FVector3 BurnProjectileColor = Pico::FVector3(1.0f, 0.12f, 0.04f);

    PPROPERTY()
    bool bFreezeShotEnabled = true;

    PPROPERTY()
    float FreezeManaCost = 25.0f;

    PPROPERTY()
    float FreezeCooldownSeconds = 3.0f;

    PPROPERTY()
    float FreezeRange = 900.0f;

    PPROPERTY()
    float FreezeProjectileSpeed = 650.0f;

    PPROPERTY()
    float FreezeEffectDuration = 6.0f;

    PPROPERTY()
    Pico::FVector3 FreezeProjectileColor = Pico::FVector3(0.08f, 0.55f, 1.0f);

    Pico::FThirdPersonControlProfileData ActiveControlProfile;
    Pico::uint64 ActiveControlProfileHash = 0;
    bool bLoadedControlProfile = false;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedHealth = 100.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedMana = 100.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedMoveSpeed = 250.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    Pico::int32 ReplicatedTagBits = 0;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedStunRemaining = 0.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedBurnRemaining = 0.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedGravityRemaining = 0.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedDashCooldownRemaining = 0.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedFireballCooldownRemaining = 0.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    float ReplicatedStunCooldownRemaining = 0.0f;

    PPROPERTY(Replicated, Transient, NotSerializable, ReadOnly, RepNotify=OnRep_GameplayState)
    Pico::int32 GameplayStateRevision = 0;

    Pico::FGameplayPredictionLedger PredictionLedger;
};
}
