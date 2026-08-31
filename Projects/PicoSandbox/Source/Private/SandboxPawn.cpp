#include "PicoSandbox/SandboxPawn.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Log.h"
#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"
#include "Pico/GameplayAbilities/GameplayTag.h"
#include "Pico/Object/ObjectInitializer.h"
#include "Pico/Object/ObjectGlobals.h"
#include "PicoSandbox/SandboxGameplayAbilities.h"
#include "PicoSandbox/SandboxFireballActor.h"

#include <algorithm>
#include <cmath>

namespace PicoSandbox
{
const char* ToString(EMovementReference Reference)
{
    return Pico::ToString(Reference).data();
}

PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PCharacter(Params)
{
    SetReplicates(true);
    PrimaryActorTick.SetCanEverTick(true);
    PrimaryActorTick.SetTickEnabled(true);
    Pico::FAssetPath::TryParse(
        "/Game/Controls/ThirdPersonDefault.pcontrolprofile",
        ThirdPersonControlProfileAsset);
}

void PSandboxPawn::BeginPlay()
{
    PCharacter::BeginPlay();
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    if (AbilitySystem == nullptr || !AbilitySystem->InitAbilityActorInfo(this, this)) return;
    SanitizeAbilityProfile();
    if (Pico::PAttributeSet* Attributes = AbilitySystem->GetAttributeSet())
    {
        Attributes->SetBaseValue(Pico::EGameplayAttribute::MaxHealth, InitialHealth,
            Pico::FName("Profile.InitialHealth"));
        Attributes->SetBaseValue(Pico::EGameplayAttribute::Health, InitialHealth,
            Pico::FName("Profile.InitialHealth"));
        Attributes->SetBaseValue(Pico::EGameplayAttribute::Mana, InitialMana,
            Pico::FName("Profile.InitialMana"));
    }
    SetAbilityLoadoutBits(BuildAbilityLoadoutBits());
    ApplyAbilitySpecOverrides();
    if (GetLocalRole() == Pico::ENetRole::Authority)
        RefreshReplicatedGameplayState();
    else
        ApplyReplicatedGameplayState();
}

void PSandboxPawn::Tick(float DeltaSeconds)
{
    PCharacter::Tick(DeltaSeconds);
    if (GetLocalRole() == Pico::ENetRole::Authority)
        RefreshReplicatedGameplayState();
}

void PSandboxPawn::EndPlay()
{
    PredictionLedger.Reset();
    if (Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent())
    {
        AbilitySystem->CancelAllAbilities();
        AbilitySystem->RemoveAllActiveGameplayEffects();
    }
    PCharacter::EndPlay();
}

EMovementReference PSandboxPawn::GetMovementReference() const
{
    if (MovementReferenceValue < static_cast<Pico::int32>(EMovementReference::ControlRotation)
        || MovementReferenceValue > static_cast<Pico::int32>(EMovementReference::World))
    {
        return EMovementReference::ControlRotation;
    }
    return static_cast<EMovementReference>(MovementReferenceValue);
}

void PSandboxPawn::SetMovementReference(EMovementReference Reference)
{
    MovementReferenceValue = static_cast<Pico::int32>(Reference);
}

const Pico::FAssetPath& PSandboxPawn::GetThirdPersonControlProfileAsset() const
{
    return ThirdPersonControlProfileAsset;
}

void PSandboxPawn::SetThirdPersonControlProfileAsset(const Pico::FAssetPath& AssetPath)
{
    ThirdPersonControlProfileAsset = AssetPath;
    bLoadedControlProfile = false;
}

bool PSandboxPawn::LoadAndApplyThirdPersonControlProfile()
{
    Pico::PWorld* World = GetWorld();
    Pico::FAssetRegistry* Registry = World != nullptr ? World->GetAssetRegistry() : nullptr;
    Pico::FAssetManager* Manager = World != nullptr ? World->GetAssetManager() : nullptr;
    if (!ThirdPersonControlProfileAsset.IsValid()
        || Registry == nullptr || Manager == nullptr)
    {
        bLoadedControlProfile = false;
        PICO_LOG(LogNet, Warning,
            "Control profile unavailable for '{}': asset='{}' registry={} manager={}",
            GetPathName(), ThirdPersonControlProfileAsset.ToString(),
            Registry != nullptr, Manager != nullptr);
        return false;
    }
    const auto Profile = Manager->LoadThirdPersonControlProfile(
        ThirdPersonControlProfileAsset, *Registry);
    if (Profile == nullptr)
    {
        bLoadedControlProfile = false;
        PICO_LOG(LogNet, Warning,
            "Control profile load failed for '{}': asset='{}'",
            GetPathName(), ThirdPersonControlProfileAsset.ToString());
        return false;
    }
    ActiveControlProfile = *Profile;
    ActiveControlProfileHash = Pico::HashThirdPersonControlProfile(*Profile);
    bLoadedControlProfile = true;
    SetMovementReference(Profile->MovementReference);
    SetUseControllerRotationYaw(Profile->bUseControllerRotationYaw);
    if (Pico::PCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->SetNetworkPolicyHash(ActiveControlProfileHash);
        Movement->SetMaxWalkSpeed(Profile->MaxWalkSpeed);
        Movement->SetRotationRate(Profile->RotationRate);
        Movement->SetOrientRotationToMovement(Profile->bOrientRotationToMovement);
    }
    PICO_LOG(LogNet, Info,
        "Control profile applied to '{}': asset='{}' policy={}",
        GetPathName(), ThirdPersonControlProfileAsset.ToString(),
        ActiveControlProfileHash);
    Pico::PObject* BoomObject = Pico::FindObject(this, Pico::FName("CameraBoom"));
    auto* Boom = BoomObject != nullptr
            && BoomObject->IsA(Pico::PSpringArmComponent::StaticClass())
        ? static_cast<Pico::PSpringArmComponent*>(BoomObject) : nullptr;
    if (Boom != nullptr)
    {
        Boom->SetTargetArmLength(Profile->DefaultCameraArmLength);
        Boom->SetUsePawnControlRotation(Profile->bCameraUsesControlRotation);
    }
    return true;
}

const Pico::FThirdPersonControlProfileData&
PSandboxPawn::GetActiveControlProfile() const
{
    return ActiveControlProfile;
}

Pico::uint64 PSandboxPawn::GetActiveControlProfileHash() const
{
    return ActiveControlProfileHash;
}

bool PSandboxPawn::HasLoadedControlProfile() const
{
    return bLoadedControlProfile;
}

Pico::PGameplayAbilitySystemComponent* PSandboxPawn::GetAbilitySystemComponent() const
{
    Pico::PObject* Object = Pico::FindObject(
        const_cast<PSandboxPawn*>(this), Pico::FName("AbilitySystem"));
    return Object != nullptr
            && Object->IsA(Pico::PGameplayAbilitySystemComponent::StaticClass())
        ? static_cast<Pico::PGameplayAbilitySystemComponent*>(Object) : nullptr;
}

bool PSandboxPawn::IsStunned() const
{
    return IsFrozen();
}

bool PSandboxPawn::IsFrozen() const
{
    const auto Tag = Pico::FGameplayTagsManager::Get().RequestGameplayTag("State.Frozen");
    const Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    return AbilitySystem != nullptr && AbilitySystem->HasMatchingGameplayTag(Tag);
}

bool PSandboxPawn::IsBurning() const
{
    const auto Tag = Pico::FGameplayTagsManager::Get().RequestGameplayTag("State.Burning");
    const auto* AbilitySystem = GetAbilitySystemComponent();
    return AbilitySystem != nullptr && AbilitySystem->HasMatchingGameplayTag(Tag);
}

bool PSandboxPawn::IsGravityAffected() const
{
    const auto Tag = Pico::FGameplayTagsManager::Get().RequestGameplayTag("State.GravityLifted");
    const auto* AbilitySystem = GetAbilitySystemComponent();
    return AbilitySystem != nullptr && AbilitySystem->HasMatchingGameplayTag(Tag);
}

float PSandboxPawn::GetReplicatedHealth() const { return ReplicatedHealth; }
float PSandboxPawn::GetReplicatedMana() const { return ReplicatedMana; }
float PSandboxPawn::GetReplicatedBurnRemaining() const { return ReplicatedBurnRemaining; }
float PSandboxPawn::GetReplicatedGravityRemaining() const { return ReplicatedGravityRemaining; }
float PSandboxPawn::GetReplicatedFreezeRemaining() const { return ReplicatedStunRemaining; }
float PSandboxPawn::GetReplicatedGravityCooldownRemaining() const
{ return ReplicatedDashCooldownRemaining; }
float PSandboxPawn::GetReplicatedBurnCooldownRemaining() const
{ return ReplicatedFireballCooldownRemaining; }
float PSandboxPawn::GetReplicatedFreezeCooldownRemaining() const
{ return ReplicatedStunCooldownRemaining; }

Pico::int32 PSandboxPawn::GetAbilityLoadoutBits() const { return AbilityLoadoutBits; }

void PSandboxPawn::OnRep_AbilityLoadout()
{
    SetAbilityLoadoutBits(AbilityLoadoutBits);
}

void PSandboxPawn::SanitizeAbilityProfile()
{
    const auto NonNegative = [](float Value, float Fallback)
    {
        return std::isfinite(Value) ? std::max(0.0f, Value) : Fallback;
    };
    const auto Positive = [](float Value, float Fallback)
    {
        return std::isfinite(Value) ? std::max(0.01f, Value) : Fallback;
    };
    const auto Color = [](Pico::FVector3& Value, const Pico::FVector3& Fallback)
    {
        if (!std::isfinite(Value.X)
            || !std::isfinite(Value.Y)
            || !std::isfinite(Value.Z))
            Value = Fallback;
        Value.X = std::clamp(Value.X, 0.0f, 1.0f);
        Value.Y = std::clamp(Value.Y, 0.0f, 1.0f);
        Value.Z = std::clamp(Value.Z, 0.0f, 1.0f);
    };
    InitialHealth = NonNegative(InitialHealth, 100.0f);
    InitialMana = NonNegative(InitialMana, 100.0f);
    GravityManaCost = NonNegative(GravityManaCost, 20.0f);
    GravityCooldownSeconds = NonNegative(GravityCooldownSeconds, 1.5f);
    GravityRange = Positive(GravityRange, 900.0f);
    GravityProjectileSpeed = Positive(GravityProjectileSpeed, 650.0f);
    GravityEffectDuration = NonNegative(GravityEffectDuration, 2.5f);
    GravityLaunchVelocity = NonNegative(GravityLaunchVelocity, 260.0f);
    Color(GravityProjectileColor, {0.70f, 0.18f, 1.0f});

    BurnManaCost = NonNegative(BurnManaCost, 15.0f);
    BurnCooldownSeconds = NonNegative(BurnCooldownSeconds, 1.0f);
    BurnRange = Positive(BurnRange, 900.0f);
    BurnProjectileSpeed = Positive(BurnProjectileSpeed, 650.0f);
    BurnEffectDuration = NonNegative(BurnEffectDuration, 8.0f);
    BurnTickInterval = Positive(BurnTickInterval, 1.0f);
    BurnDamagePerTick = NonNegative(BurnDamagePerTick, 5.0f);
    Color(BurnProjectileColor, {1.0f, 0.12f, 0.04f});

    FreezeManaCost = NonNegative(FreezeManaCost, 25.0f);
    FreezeCooldownSeconds = NonNegative(FreezeCooldownSeconds, 3.0f);
    FreezeRange = Positive(FreezeRange, 900.0f);
    FreezeProjectileSpeed = Positive(FreezeProjectileSpeed, 650.0f);
    FreezeEffectDuration = NonNegative(FreezeEffectDuration, 6.0f);
    Color(FreezeProjectileColor, {0.08f, 0.55f, 1.0f});
}

Pico::int32 PSandboxPawn::BuildAbilityLoadoutBits() const
{
    return (bGravityShotEnabled ? 1 : 0)
        | (bBurnShotEnabled ? 2 : 0)
        | (bFreezeShotEnabled ? 4 : 0);
}

void PSandboxPawn::ApplyAbilitySpecOverrides()
{
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    if (AbilitySystem == nullptr) return;
    for (const Pico::FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
    {
        if (Spec.AbilityClass == PSandboxDashAbility::StaticClass())
            AbilitySystem->ConfigureAbilitySpec(
                Spec.Handle, GravityManaCost, GravityCooldownSeconds);
        else if (Spec.AbilityClass == PSandboxFireballAbility::StaticClass())
            AbilitySystem->ConfigureAbilitySpec(
                Spec.Handle, BurnManaCost, BurnCooldownSeconds);
        else if (Spec.AbilityClass == PSandboxStunAbility::StaticClass())
            AbilitySystem->ConfigureAbilitySpec(
                Spec.Handle, FreezeManaCost, FreezeCooldownSeconds);
    }
}

bool PSandboxPawn::SetAbilityLoadoutBits(Pico::int32 Bits)
{
    if (Bits < 0 || Bits > 7) return false;
    const bool bLoadoutChanged = AbilityLoadoutBits != Bits;
    AbilityLoadoutBits = Bits;
    if (bLoadoutChanged)
        MarkReplicatedPropertyDirty(Pico::FName("AbilityLoadoutBits"));
    bGravityShotEnabled = (Bits & 1) != 0;
    bBurnShotEnabled = (Bits & 2) != 0;
    bFreezeShotEnabled = (Bits & 4) != 0;
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    if (AbilitySystem == nullptr) return true;
    struct FEntry { Pico::int32 Bit; const Pico::PClass* Class; Pico::int32 InputId; };
    const FEntry Entries[] = {
        {1, PSandboxDashAbility::StaticClass(), 0},
        {2, PSandboxFireballAbility::StaticClass(), 1},
        {4, PSandboxStunAbility::StaticClass(), 2}};
    for (const FEntry& Entry : Entries)
    {
        Pico::FGameplayAbilitySpecHandle Existing;
        for (const Pico::FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
            if (Spec.AbilityClass == Entry.Class) { Existing = Spec.Handle; break; }
        if ((Bits & Entry.Bit) != 0 && !Existing.IsValid())
            AbilitySystem->GiveAbility(Entry.Class, 1, Entry.InputId);
        else if ((Bits & Entry.Bit) == 0 && Existing.IsValid())
            AbilitySystem->ClearAbility(Existing);
    }
    ApplyAbilitySpecOverrides();
    return true;
}

bool PSandboxPawn::ActivateImmediateAbility(const Pico::PClass* AbilityClass)
{
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    if (AbilitySystem == nullptr || AbilityClass == nullptr) return false;
    for (const Pico::FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
    {
        if (Spec.AbilityClass == AbilityClass)
        {
            if (!AbilitySystem->TryActivateAbility(Spec.Handle)) return false;
            return AbilitySystem->EndAbility(Spec.Handle);
        }
    }
    return false;
}

bool PSandboxPawn::SpawnAuthorityProjectile(
    PSandboxPawn* Target,
    const Pico::PClass* AbilityClass,
    ESandboxProjectileEffect Effect,
    float MaxRange)
{
    if (GetLocalRole() != Pico::ENetRole::Authority || Target == nullptr
        || Target == this || Target->GetLocalRole() != Pico::ENetRole::Authority
        || (Target->GetActorLocation() - GetActorLocation()).Size() > MaxRange
        || !ActivateImmediateAbility(AbilityClass))
        return false;
    Pico::PWorld* World = GetWorld();
    if (World == nullptr) return false;
    Pico::FActorSpawnParameters Spawn;
    Spawn.Name = Pico::FName("AbilityProjectile_" + std::to_string(World->GetTickCount())
        + "_" + std::to_string(static_cast<Pico::int32>(Effect)));
    Spawn.Owner = this;
    auto* Projectile = World->SpawnActor<PSandboxFireballActor>(Spawn);
    float ProjectileSpeed = BurnProjectileSpeed;
    Pico::FVector3 ProjectileColor = BurnProjectileColor;
    if (Effect == ESandboxProjectileEffect::Gravity)
    {
        ProjectileSpeed = GravityProjectileSpeed;
        ProjectileColor = GravityProjectileColor;
    }
    else if (Effect == ESandboxProjectileEffect::Freeze)
    {
        ProjectileSpeed = FreezeProjectileSpeed;
        ProjectileColor = FreezeProjectileColor;
    }
    const bool bSpawned = Projectile != nullptr
        && Projectile->InitializeProjectile(
            this, Target, Effect, ProjectileSpeed, ProjectileColor);
    if (Projectile != nullptr && !bSpawned) Projectile->Destroy();
    RefreshReplicatedGameplayState();
    return bSpawned;
}

bool PSandboxPawn::TryAuthorityGravityShot(PSandboxPawn* Target)
{
    return SpawnAuthorityProjectile(Target, PSandboxDashAbility::StaticClass(),
        ESandboxProjectileEffect::Gravity, GravityRange);
}

bool PSandboxPawn::TryAuthorityBurnShot(PSandboxPawn* Target)
{
    return SpawnAuthorityProjectile(Target, PSandboxFireballAbility::StaticClass(),
        ESandboxProjectileEffect::Burn, BurnRange);
}

bool PSandboxPawn::TryAuthorityFreezeShot(PSandboxPawn* Target)
{
    return SpawnAuthorityProjectile(Target, PSandboxStunAbility::StaticClass(),
        ESandboxProjectileEffect::Freeze, FreezeRange);
}

bool PSandboxPawn::ApplyAuthorityProjectileEffect(
    PSandboxPawn* Source,
    ESandboxProjectileEffect Effect)
{
    if (GetLocalRole() != Pico::ENetRole::Authority || Source == nullptr) return false;
    Pico::PGameplayAbilitySystemComponent* TargetSystem = GetAbilitySystemComponent();
    if (TargetSystem == nullptr) return false;
    Pico::FGameplayEffectSpec Spec = TargetSystem->MakeOutgoingSpec(
        Pico::PGameplayEffect::StaticClass(), 1.0f, Source);
    Spec.DurationPolicy = Pico::EGameplayEffectDurationPolicy::Duration;
    if (Effect == ESandboxProjectileEffect::Burn)
    {
        Spec.Duration = Source->BurnEffectDuration;
        Spec.Period = Source->BurnTickInterval;
        Spec.StackingKey = "Damage.Burning";
        Spec.StackLimitCount = 1;
        Spec.Modifiers = {{Pico::EGameplayAttribute::Health,
            Pico::EGameplayModifierOperation::Add, -Source->BurnDamagePerTick}};
        Spec.GrantedTags.AddTag(Pico::FGameplayTagsManager::Get()
            .RequestGameplayTag("State.Burning"));
    }
    else if (Effect == ESandboxProjectileEffect::Gravity)
    {
        Spec.Duration = Source->GravityEffectDuration;
        Spec.StackingKey = "Control.GravityLift";
        Spec.GrantedTags.AddTag(Pico::FGameplayTagsManager::Get()
            .RequestGameplayTag("State.GravityLifted"));
    }
    else
    {
        Spec.Duration = Source->FreezeEffectDuration;
        Spec.StackingKey = "Control.Freeze";
        Spec.GrantedTags.AddTag(Pico::FGameplayTagsManager::Get()
            .RequestGameplayTag("State.Frozen"));
    }
    const bool bApplied = TargetSystem->ApplyGameplayEffectSpecToSelf(Spec);
    if (bApplied && Effect == ESandboxProjectileEffect::Gravity)
    {
        if (Pico::PCharacterMovementComponent* Movement = GetCharacterMovement())
        {
            Pico::FVector3 Velocity = Movement->GetVelocity();
            Velocity.Z = std::max(Velocity.Z, Source->GravityLaunchVelocity);
            Movement->SetVelocity(Velocity);
            Movement->SetMovementMode(Pico::EMovementMode::Falling);
        }
    }
    if (bApplied) RefreshReplicatedGameplayState();
    return bApplied;
}

Pico::FGameplayPredictionKey PSandboxPawn::BeginPredictedDash(
    const Pico::FVector3& Direction)
{
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    Pico::PAttributeSet* Attributes = AbilitySystem != nullptr
        ? AbilitySystem->GetAttributeSet() : nullptr;
    if (GetLocalRole() != Pico::ENetRole::AutonomousProxy || Attributes == nullptr
        || Direction.IsNearlyZero())
        return {};
    Pico::FGameplayAbilityPredictionSnapshot Snapshot;
    Snapshot.Transform = GetActorTransform();
    Snapshot.Health = Attributes->GetCurrentValue(Pico::EGameplayAttribute::Health);
    Snapshot.Mana = Attributes->GetCurrentValue(Pico::EGameplayAttribute::Mana);
    Snapshot.OwnedTags = AbilitySystem->GetOwnedGameplayTags().ExportText();
    if (!ActivateImmediateAbility(PSandboxDashAbility::StaticClass())) return {};
    const Pico::FGameplayPredictionKey Key = PredictionLedger.BeginPrediction(Snapshot);
    if (Pico::PCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Pico::FCharacterMoveInput Input;
        Input.RootMotionDelta = Pico::FTransform(Direction.GetSafeNormal() * 260.0f);
        Movement->SimulateMovement(Input, 1.0f / 60.0f);
    }
    else
        SetActorLocation(GetActorLocation() + Direction.GetSafeNormal() * 260.0f);
    return Key;
}

bool PSandboxPawn::ResolvePredictedDash(
    Pico::FGameplayPredictionKey Key,
    bool bAccepted,
    const Pico::FVector3& AuthorityLocation,
    float AuthorityMana)
{
    Pico::FGameplayAbilityPredictionSnapshot Rollback;
    const auto Result = PredictionLedger.ResolvePrediction(Key, bAccepted, &Rollback);
    if (Result == Pico::EGameplayPredictionResult::Unknown) return false;
    if (!bAccepted)
    {
        SetActorLocation(AuthorityLocation);
        if (Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent())
        {
            AbilitySystem->RemoveAllActiveGameplayEffects();
            if (Pico::PAttributeSet* Attributes = AbilitySystem->GetAttributeSet())
                Attributes->SetCurrentValue(
                    Pico::EGameplayAttribute::Mana, AuthorityMana,
                    Pico::FName("Network.DashRejected"));
        }
    }
    return true;
}

void PSandboxPawn::RefreshReplicatedGameplayState()
{
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    Pico::PAttributeSet* Attributes = AbilitySystem != nullptr
        ? AbilitySystem->GetAttributeSet() : nullptr;
    if (Attributes == nullptr) return;
    const float Health = Attributes->GetCurrentValue(Pico::EGameplayAttribute::Health);
    const float Mana = Attributes->GetCurrentValue(Pico::EGameplayAttribute::Mana);
    const float MoveSpeed = Attributes->GetCurrentValue(Pico::EGameplayAttribute::MoveSpeed);
    static constexpr Pico::int32 FrozenBit = 1 << 0;
    static constexpr Pico::int32 DashCooldownBit = 1 << 1;
    static constexpr Pico::int32 FireballCooldownBit = 1 << 2;
    static constexpr Pico::int32 StunCooldownBit = 1 << 3;
    static constexpr Pico::int32 BurningBit = 1 << 4;
    static constexpr Pico::int32 GravityBit = 1 << 5;
    const auto& Manager = Pico::FGameplayTagsManager::Get();
    const auto FrozenTag = Manager.RequestGameplayTag("State.Frozen");
    const auto BurningTag = Manager.RequestGameplayTag("State.Burning");
    const auto GravityTag = Manager.RequestGameplayTag("State.GravityLifted");
    const auto DashCooldownTag = Manager.RequestGameplayTag("Cooldown.PSandboxDashAbility");
    const auto FireballCooldownTag = Manager.RequestGameplayTag("Cooldown.PSandboxFireballAbility");
    const auto StunCooldownTag = Manager.RequestGameplayTag("Cooldown.PSandboxStunAbility");
    Pico::int32 TagBits = 0;
    if (AbilitySystem->HasMatchingGameplayTag(FrozenTag)) TagBits |= FrozenBit;
    if (AbilitySystem->HasMatchingGameplayTag(BurningTag)) TagBits |= BurningBit;
    if (AbilitySystem->HasMatchingGameplayTag(GravityTag)) TagBits |= GravityBit;
    if (AbilitySystem->HasMatchingGameplayTag(DashCooldownTag)) TagBits |= DashCooldownBit;
    if (AbilitySystem->HasMatchingGameplayTag(FireballCooldownTag)) TagBits |= FireballCooldownBit;
    if (AbilitySystem->HasMatchingGameplayTag(StunCooldownTag)) TagBits |= StunCooldownBit;
    float StunRemaining = 0.0f;
    float BurnRemaining = 0.0f;
    float GravityRemaining = 0.0f;
    float DashCooldown = 0.0f;
    float FireballCooldown = 0.0f;
    float StunCooldown = 0.0f;
    for (const Pico::FActiveGameplayEffect& Active : AbilitySystem->GetActiveGameplayEffects())
    {
        if (Active.Spec.StackingKey == "Control.Freeze")
            StunRemaining = std::max(StunRemaining, Active.RemainingDuration);
        else if (Active.Spec.StackingKey == "Damage.Burning")
            BurnRemaining = std::max(BurnRemaining, Active.RemainingDuration);
        else if (Active.Spec.StackingKey == "Control.GravityLift")
            GravityRemaining = std::max(GravityRemaining, Active.RemainingDuration);
        else if (Active.Spec.StackingKey == "Cooldown.PSandboxDashAbility")
            DashCooldown = std::max(DashCooldown, Active.RemainingDuration);
        else if (Active.Spec.StackingKey == "Cooldown.PSandboxFireballAbility")
            FireballCooldown = std::max(FireballCooldown, Active.RemainingDuration);
        else if (Active.Spec.StackingKey == "Cooldown.PSandboxStunAbility")
            StunCooldown = std::max(StunCooldown, Active.RemainingDuration);
    }
    bool bChanged = false;
    const auto UpdateFloat = [this, &bChanged](
        float& Field, float Value, float Tolerance, const char* PropertyName)
    {
        if (std::abs(Field - Value) <= Tolerance) return;
        Field = Value;
        bChanged = true;
        MarkReplicatedPropertyDirty(Pico::FName(PropertyName));
    };
    const auto UpdateInt = [this, &bChanged](
        Pico::int32& Field, Pico::int32 Value, const char* PropertyName)
    {
        if (Field == Value) return;
        Field = Value;
        bChanged = true;
        MarkReplicatedPropertyDirty(Pico::FName(PropertyName));
    };
    UpdateFloat(ReplicatedHealth, Health, 0.001f, "ReplicatedHealth");
    UpdateFloat(ReplicatedMana, Mana, 0.001f, "ReplicatedMana");
    UpdateFloat(ReplicatedMoveSpeed, MoveSpeed, 0.001f, "ReplicatedMoveSpeed");
    UpdateInt(ReplicatedTagBits, TagBits, "ReplicatedTagBits");

    // Keep countdown replication bounded while allowing sub-threshold frame
    // deltas to accumulate against the last published value.
    UpdateFloat(ReplicatedStunRemaining, StunRemaining, 0.05f,
        "ReplicatedStunRemaining");
    UpdateFloat(ReplicatedBurnRemaining, BurnRemaining, 0.05f,
        "ReplicatedBurnRemaining");
    UpdateFloat(ReplicatedGravityRemaining, GravityRemaining, 0.05f,
        "ReplicatedGravityRemaining");
    UpdateFloat(ReplicatedDashCooldownRemaining, DashCooldown, 0.05f,
        "ReplicatedDashCooldownRemaining");
    UpdateFloat(ReplicatedFireballCooldownRemaining, FireballCooldown, 0.05f,
        "ReplicatedFireballCooldownRemaining");
    UpdateFloat(ReplicatedStunCooldownRemaining, StunCooldown, 0.05f,
        "ReplicatedStunCooldownRemaining");
    if (bChanged)
    {
        ++GameplayStateRevision;
        MarkReplicatedPropertyDirty(Pico::FName("GameplayStateRevision"));
    }
}

void PSandboxPawn::ApplyReplicatedGameplayState()
{
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
    Pico::PAttributeSet* Attributes = AbilitySystem != nullptr
        ? AbilitySystem->GetAttributeSet() : nullptr;
    if (AbilitySystem == nullptr || Attributes == nullptr) return;
    Attributes->SetCurrentValue(Pico::EGameplayAttribute::Health,
        ReplicatedHealth, Pico::FName("Network.Replication"));
    Attributes->SetCurrentValue(Pico::EGameplayAttribute::Mana,
        ReplicatedMana, Pico::FName("Network.Replication"));
    Attributes->SetCurrentValue(Pico::EGameplayAttribute::MoveSpeed,
        ReplicatedMoveSpeed, Pico::FName("Network.Replication"));
    const auto ApplyBit = [AbilitySystem, this](
        const char* Name, Pico::int32 Bit)
    {
        const Pico::FGameplayTag Tag =
            Pico::FGameplayTagsManager::Get().RegisterGameplayTag(Name);
        const bool bShouldHave = (ReplicatedTagBits & Bit) != 0;
        const int Count = AbilitySystem->GetGameplayTagCount(Tag);
        if (bShouldHave && Count == 0) AbilitySystem->AddLooseGameplayTag(Tag);
        if (!bShouldHave && Count > 0) AbilitySystem->RemoveLooseGameplayTag(Tag, Count);
    };
    ApplyBit("State.Frozen", 1 << 0);
    ApplyBit("Cooldown.PSandboxDashAbility", 1 << 1);
    ApplyBit("Cooldown.PSandboxFireballAbility", 1 << 2);
    ApplyBit("Cooldown.PSandboxStunAbility", 1 << 3);
    ApplyBit("State.Burning", 1 << 4);
    ApplyBit("State.GravityLifted", 1 << 5);
}

void PSandboxPawn::OnRep_GameplayState()
{
    ApplyReplicatedGameplayState();
}

bool PSandboxPawn::DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer)
{
    Pico::PCapsuleComponent* Root =
        Initializer.CreateDefaultSubobject<Pico::PCapsuleComponent>("CollisionCapsule");
    Pico::PStaticMeshComponent* LegacyMesh =
        Initializer.CreateDefaultSubobject<Pico::PStaticMeshComponent>("SandboxPlayerMesh");
    Pico::PSkeletalMeshComponent* AnimatedMesh =
        Initializer.CreateDefaultSubobject<Pico::PSkeletalMeshComponent>("SandboxAnimatedMesh");
    Pico::PCharacterMovementComponent* Movement =
        Initializer.CreateDefaultSubobject<Pico::PCharacterMovementComponent>(
            "CharacterMovement");
    Pico::PGameplayAbilitySystemComponent* AbilitySystem =
        Initializer.CreateDefaultSubobject<Pico::PGameplayAbilitySystemComponent>(
            "AbilitySystem");
    Pico::PSpringArmComponent* CameraBoom =
        Initializer.CreateDefaultSubobject<Pico::PSpringArmComponent>("CameraBoom");
    Pico::PCameraComponent* FollowCamera =
        Initializer.CreateDefaultSubobject<Pico::PCameraComponent>("FollowCamera");
    if (Root == nullptr
        || LegacyMesh == nullptr
        || AnimatedMesh == nullptr
        || Movement == nullptr
        || AbilitySystem == nullptr
        || CameraBoom == nullptr
        || FollowCamera == nullptr)
    {
        return false;
    }

    LegacyMesh->SetVisible(false);
    LegacyMesh->SetCollisionEnabled(Pico::ECollisionEnabled::NoCollision);
    CameraBoom->SetTargetArmLength(420.0f);
    CameraBoom->SetTargetOffset({0.0f, 0.0f, 90.0f});
    CameraBoom->SetRelativeRotation({-15.0f, 0.0f, 0.0f});
    CameraBoom->SetUsePawnControlRotation(true);
    FollowCamera->SetActive(true);
    Root->SetCollisionProfile(Pico::ECollisionProfile::Pawn);
    Root->SetPhysicsBodyType(Pico::EPhysicsBodyType::Kinematic);
    Root->SetPhysicsContactEnabled(false);
    Root->SetGravityEnabled(false);
    Movement->SetMaxWalkSpeed(250.0f);
    Movement->SetOrientRotationToMovement(true);
    Movement->SetRotationRate(540.0f);
    SetUseControllerRotationYaw(false);
    return Initializer.SetRootSubobject(Root)
        && Initializer.AttachSubobject(LegacyMesh, Root)
        && Initializer.AttachSubobject(AnimatedMesh, Root)
        && Initializer.AttachSubobject(CameraBoom, Root)
        && Initializer.AttachSubobject(
            FollowCamera, CameraBoom, Pico::PSpringArmComponent::GetEndpointSocketName());
}

void PSandboxPawn::PostLoad()
{
    PCharacter::PostLoad();
    SetMovementReference(GetMovementReference());
    if (AbilityLoadoutBits != 7
        && bGravityShotEnabled && bBurnShotEnabled && bFreezeShotEnabled)
    {
        bGravityShotEnabled = (AbilityLoadoutBits & 1) != 0;
        bBurnShotEnabled = (AbilityLoadoutBits & 2) != 0;
        bFreezeShotEnabled = (AbilityLoadoutBits & 4) != 0;
    }
    SanitizeAbilityProfile();
    AbilityLoadoutBits = BuildAbilityLoadoutBits();
    Pico::PObject* Object = Pico::FindObject(this, Pico::FName("SandboxPlayerMesh"));
    if (Object != nullptr && Object->IsA(Pico::PStaticMeshComponent::StaticClass()))
    {
        auto* LegacyMesh = static_cast<Pico::PStaticMeshComponent*>(Object);
        LegacyMesh->SetVisible(false);
        LegacyMesh->SetCollisionEnabled(Pico::ECollisionEnabled::NoCollision);
        LegacyMesh->SetStaticMeshAsset({});
        LegacyMesh->SetMaterialAsset({});
    }
}
}
