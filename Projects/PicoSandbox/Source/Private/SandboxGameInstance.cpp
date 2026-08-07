#include "PicoSandbox/SandboxGameInstance.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
#include "PicoSandbox/SandboxPawn.h"

namespace PicoSandbox
{
bool FSandboxGameInstance::Init(Pico::FGameEngine& GameEngine)
{
    Pico::PWorld* World = GameEngine.GetEngineLoop().GetWorld();
    if (World == nullptr)
    {
        return false;
    }

    PSandboxPawn* Pawn = World->SpawnActor<PSandboxPawn>("SandboxPlayer");
    if (Pawn == nullptr)
    {
        return false;
    }

    Pico::PStaticMeshComponent* Mesh =
        Pawn->CreateComponent<Pico::PStaticMeshComponent>("SandboxPlayerMesh");
    Pico::FAssetPath MeshAsset;
    Pico::FAssetPath MaterialAsset;
    if (Mesh == nullptr
        || !Pico::FAssetPath::TryParse(
            "/Game/Meshes/spot_triangulated_good.pmesh", MeshAsset)
        || !Pico::FAssetPath::TryParse(
            "/Game/Materials/cow_1.pmat", MaterialAsset))
    {
        Pawn->Destroy();
        return false;
    }

    Mesh->SetStaticMeshAsset(MeshAsset);
    Mesh->SetMaterialAsset(MaterialAsset);
    if (!Pawn->SetRootComponent(Mesh))
    {
        Pawn->Destroy();
        return false;
    }

    Pawn->SetActorLocation(Pico::FVector3(0.0f, 0.0f, 0.0f));
    Pawn->SetInputSystem(&GameEngine.GetInputSystem());
    PawnHandle = Pawn->GetHandle();
    return true;
}

void FSandboxGameInstance::Shutdown()
{
    if (PSandboxPawn* Pawn = GetPawn())
    {
        Pawn->SetInputSystem(nullptr);
    }
    PawnHandle = {};
}

PSandboxPawn* FSandboxGameInstance::GetPawn() const
{
    Pico::PObject* Object = Pico::ResolveObject(PawnHandle);
    return Object != nullptr && Object->IsA(PSandboxPawn::StaticClass())
        ? static_cast<PSandboxPawn*>(Object)
        : nullptr;
}
}
