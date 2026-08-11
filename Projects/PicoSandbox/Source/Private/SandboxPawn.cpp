#include "PicoSandbox/SandboxPawn.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Object/ObjectInitializer.h"

namespace PicoSandbox
{
PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PCharacter(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

bool PSandboxPawn::DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer)
{
    Pico::PCapsuleComponent* Root =
        Initializer.CreateDefaultSubobject<Pico::PCapsuleComponent>("CollisionCapsule");
    Pico::PStaticMeshComponent* Mesh =
        Initializer.CreateDefaultSubobject<Pico::PStaticMeshComponent>("SandboxPlayerMesh");
    Pico::PSkeletalMeshComponent* AnimatedMesh =
        Initializer.CreateDefaultSubobject<Pico::PSkeletalMeshComponent>("SandboxAnimatedMesh");
    Pico::PCharacterMovementComponent* Movement =
        Initializer.CreateDefaultSubobject<Pico::PCharacterMovementComponent>(
            "CharacterMovement");
    Pico::FAssetPath MeshAsset;
    Pico::FAssetPath MaterialAsset;
    if (Root == nullptr
        || Mesh == nullptr
        || AnimatedMesh == nullptr
        || Movement == nullptr
        || !Pico::FAssetPath::TryParse(
            "/Game/Meshes/spot_triangulated_good.pmesh", MeshAsset)
        || !Pico::FAssetPath::TryParse(
            "/Game/Materials/cow_1.pmat", MaterialAsset))
    {
        return false;
    }

    Mesh->SetStaticMeshAsset(MeshAsset);
    Mesh->SetMaterialAsset(MaterialAsset);
    AnimatedMesh->SetMaterialAsset(MaterialAsset);
    AnimatedMesh->SetRelativeLocation({0.0f, -120.0f, -96.0f});
    Root->SetCollisionEnabled(Pico::ECollisionEnabled::QueryOnly);
    Root->SetPhysicsBodyType(Pico::EPhysicsBodyType::Kinematic);
    Root->SetSensor(false);
    Root->SetGravityEnabled(false);
    Movement->SetMaxWalkSpeed(250.0f);
    return Initializer.SetRootSubobject(Root)
        && Initializer.AttachSubobject(Mesh, Root)
        && Initializer.AttachSubobject(AnimatedMesh, Root);
}
}
