#include "PicoSandbox/SandboxPawn.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/ObjectInitializer.h"

namespace PicoSandbox
{
PSandboxPawn::PSandboxPawn(const Pico::FObjectConstructionParams& Params)
    : PActor(Params)
{
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

void PSandboxPawn::SetInputSystem(Pico::FInputSystem* InInputSystem)
{
    InputSystem = InInputSystem;
}

float PSandboxPawn::GetMoveSpeed() const
{
    return MoveSpeed;
}

void PSandboxPawn::Tick(float DeltaSeconds)
{
    if (InputSystem == nullptr)
    {
        return;
    }

    const float Forward = InputSystem->GetAxisValue("MoveForward");
    const float Right = InputSystem->GetAxisValue("MoveRight");
    const Pico::FVector3 Direction(Forward, Right, 0.0f);
    if (!Direction.IsNearlyZero())
    {
        SetActorLocation(
            GetActorLocation()
                + Direction.GetSafeNormal() * MoveSpeed * DeltaSeconds);
    }
}
}
