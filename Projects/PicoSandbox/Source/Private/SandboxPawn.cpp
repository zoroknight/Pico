#include "PicoSandbox/SandboxPawn.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Object/ObjectInitializer.h"

namespace PicoSandbox
{
PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PPawn(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

bool PSandboxPawn::DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer)
{
    Pico::PSceneComponent* Root =
        Initializer.CreateDefaultSubobject<Pico::PSceneComponent>("DefaultSceneRoot");
    Pico::PStaticMeshComponent* Mesh =
        Initializer.CreateDefaultSubobject<Pico::PStaticMeshComponent>("SandboxPlayerMesh");
    Pico::FAssetPath MeshAsset;
    Pico::FAssetPath MaterialAsset;
    if (Root == nullptr
        || Mesh == nullptr
        || !Pico::FAssetPath::TryParse(
            "/Game/Meshes/spot_triangulated_good.pmesh", MeshAsset)
        || !Pico::FAssetPath::TryParse(
            "/Game/Materials/cow_1.pmat", MaterialAsset))
    {
        return false;
    }

    Mesh->SetStaticMeshAsset(MeshAsset);
    Mesh->SetMaterialAsset(MaterialAsset);
    return Initializer.SetRootSubobject(Root)
        && Initializer.AttachSubobject(Mesh, Root);
}

float PSandboxPawn::GetMoveSpeed() const
{
    return MoveSpeed;
}

void PSandboxPawn::MoveFromInput(
    float Forward,
    float Right,
    float DeltaSeconds)
{
    MoveInWorldDirection(Pico::FVector3(Forward, Right, 0.0f), DeltaSeconds);
}

void PSandboxPawn::MoveInWorldDirection(
    const Pico::FVector3& WorldDirection,
    float DeltaSeconds)
{
    if (!WorldDirection.IsNearlyZero())
    {
        SetActorLocation(
            GetActorLocation()
                + WorldDirection.GetSafeNormal() * MoveSpeed * DeltaSeconds);
    }
}
}
