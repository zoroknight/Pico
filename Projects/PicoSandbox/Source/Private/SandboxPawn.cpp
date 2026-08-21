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
#include "Pico/Object/ObjectInitializer.h"
#include "Pico/Object/ObjectGlobals.h"

namespace PicoSandbox
{
const char* ToString(EMovementReference Reference)
{
    return Pico::ToString(Reference).data();
}

PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PCharacter(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
    Pico::FAssetPath::TryParse(
        "/Game/Controls/ThirdPersonDefault.pcontrolprofile",
        ThirdPersonControlProfileAsset);
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
    Pico::PSpringArmComponent* CameraBoom =
        Initializer.CreateDefaultSubobject<Pico::PSpringArmComponent>("CameraBoom");
    Pico::PCameraComponent* FollowCamera =
        Initializer.CreateDefaultSubobject<Pico::PCameraComponent>("FollowCamera");
    if (Root == nullptr
        || LegacyMesh == nullptr
        || AnimatedMesh == nullptr
        || Movement == nullptr
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
    Root->SetCollisionEnabled(Pico::ECollisionEnabled::QueryOnly);
    Root->SetPhysicsBodyType(Pico::EPhysicsBodyType::Kinematic);
    Root->SetSensor(false);
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
