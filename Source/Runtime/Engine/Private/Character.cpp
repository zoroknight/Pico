#include "Pico/Engine/Character.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectInitializer.h"

#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PCharacter)

bool PCharacter::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, bPressedJump, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PCharacter::PCharacter(const FObjectConstructionParams& Params)
    : PPawn(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

bool PCharacter::DefineDefaultSubobjects(FObjectInitializer& Initializer)
{
    PCapsuleComponent* Capsule =
        Initializer.CreateDefaultSubobject<PCapsuleComponent>("CollisionCapsule");
    PCharacterMovementComponent* Movement =
        Initializer.CreateDefaultSubobject<PCharacterMovementComponent>("CharacterMovement");
    if (Capsule == nullptr || Movement == nullptr)
    {
        return false;
    }

    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Capsule->SetPhysicsBodyType(EPhysicsBodyType::Kinematic);
    Capsule->SetSensor(false);
    Capsule->SetGravityEnabled(false);
    return Initializer.SetRootSubobject(Capsule);
}

PCapsuleComponent* PCharacter::GetCapsuleComponent() const
{
    PSceneComponent* Root = GetRootComponent();
    return Root != nullptr && Root->IsA(PCapsuleComponent::StaticClass())
        ? static_cast<PCapsuleComponent*>(Root)
        : nullptr;
}

PCharacterMovementComponent* PCharacter::GetCharacterMovement() const
{
    for (PActorComponent* Component : GetComponents())
    {
        if (Component != nullptr
            && Component->IsA(PCharacterMovementComponent::StaticClass()))
        {
            return static_cast<PCharacterMovementComponent*>(Component);
        }
    }
    return nullptr;
}

PCharacterMovementComponent* PCharacter::GetMovementComponent() const
{
    return GetCharacterMovement();
}

void PCharacter::Jump()
{
    if (CheckGameThread("PCharacter::Jump")) bPressedJump = true;
}

void PCharacter::StopJumping()
{
    if (CheckGameThread("PCharacter::StopJumping")) bPressedJump = false;
}

bool PCharacter::IsJumpPressed() const { return bPressedJump; }

bool PCharacter::ConsumeJumpInput()
{
    if (!CheckGameThread("PCharacter::ConsumeJumpInput")) return false;
    const bool bResult = bPressedJump;
    bPressedJump = false;
    return bResult;
}
}
