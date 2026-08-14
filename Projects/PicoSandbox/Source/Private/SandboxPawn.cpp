#include "PicoSandbox/SandboxPawn.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Object/ObjectInitializer.h"
#include "Pico/Object/ObjectGlobals.h"

namespace PicoSandbox
{
const char* ToString(EMovementReference Reference)
{
    switch (Reference)
    {
    case EMovementReference::ControlRotation: return "ControlRotation";
    case EMovementReference::ActorRotation: return "ActorRotation";
    case EMovementReference::World: return "World";
    }
    return "Unknown";
}

PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PCharacter(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
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
