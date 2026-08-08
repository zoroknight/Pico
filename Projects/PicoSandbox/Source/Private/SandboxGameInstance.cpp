#include "PicoSandbox/SandboxGameInstance.h"

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

    Pico::PObject* MeshObject =
        Pico::FindObject(Pawn, Pico::FName("SandboxPlayerMesh"));
    if (MeshObject == nullptr
        || !MeshObject->IsA(Pico::PStaticMeshComponent::StaticClass()))
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
