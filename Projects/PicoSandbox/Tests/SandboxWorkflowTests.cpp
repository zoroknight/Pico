#include "TestRunner.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Property.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameModule.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/ObjectGlobals.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxEntity.h"
#include "PicoSandbox/SandboxGameInstance.h"
#include "PicoSandbox/SandboxGameMode.h"
#include "PicoSandbox/SandboxModule.h"
#include "PicoSandbox/SandboxPawn.h"
#include "PicoSandbox/SandboxPlayerController.h"
#include "PicoSandbox/SandboxSession.h"

#include <filesystem>
#include <memory>

int main()
{
    FTestRunner Runner;
    const std::filesystem::path TestPath =
        PicoSandbox::FSandboxSession::GetSandboxRootDirectory()
        / "Saved"
        / "Tests"
        / "SandboxWorkflow.pobj";

    {
        PicoSandbox::FSandboxSession Session(TestPath);
        Runner.Expect(Session.Initialize(), "Sandbox initializes Pico and registers project classes");

        const Pico::PClass* EntityClass = PicoSandbox::PSandboxEntity::StaticClass();
        const Pico::PClass* CharacterClass = PicoSandbox::PSandboxCharacter::StaticClass();
        Runner.Expect(
            Pico::FClassRegistry::FindClass(Pico::FName("PSandboxEntity")) == EntityClass
                && Pico::FClassRegistry::FindClass(Pico::FName("PSandboxCharacter")) == CharacterClass,
            "Project classes are discoverable through the engine class registry");
        Runner.Expect(
            CharacterClass->GetSuperClass() == EntityClass
                && CharacterClass->FindProperty(Pico::FName("EntityId")) != nullptr
                && CharacterClass->FindProperty(Pico::FName("Health")) != nullptr
                && CharacterClass->FindProperty(Pico::FName("Velocity")) != nullptr
                && CharacterClass->FindProperty(Pico::FName("ViewRotation")) != nullptr
                && CharacterClass->FindProperty(Pico::FName("Transform")) != nullptr,
            "Project metadata exposes inherited and local reflected properties");
        const Pico::PProperty* TransformProperty =
            CharacterClass->FindProperty(Pico::FName("Transform"));
        Runner.Expect(
            TransformProperty != nullptr
                && TransformProperty->GetType() == Pico::EPropertyType::Transform
                && TransformProperty->GetSize() == sizeof(Pico::FTransform),
            "Project transform uses typed reflection metadata");
        Runner.Expect(
            CharacterClass->FindProperty(Pico::FName("Velocity"))->GetType() == Pico::EPropertyType::Vector3
                && CharacterClass->FindProperty(Pico::FName("ViewRotation"))->GetType()
                    == Pico::EPropertyType::Rotator,
            "Project vector and rotator use typed reflection metadata");

        Runner.Expect(Session.RunFullWorkflow(), "Sandbox completes the full persistence workflow");
        Runner.Expect(std::filesystem::is_regular_file(TestPath), "Sandbox writes a .pobj inside its project directory");

        const PicoSandbox::FSandboxSnapshot Snapshot = Session.GetSnapshot();
        Runner.Expect(
            Snapshot.EntityId == 2002
                && Snapshot.Health == 75
                && Snapshot.MoveSpeed == 720.0f
                && !Snapshot.bAlive
                && Snapshot.Velocity.Equals(Pico::FVector3(100.0f, 0.0f, 25.0f))
                && Snapshot.ViewRotation.Equals(Pico::FRotator(5.0f, 90.0f, 0.0f))
                && Snapshot.Transform.Translation.Equals(Pico::FVector3(120.0f, 30.0f, 10.0f))
                && Snapshot.Transform.Rotation.Rotator().Equals(Pico::FRotator(10.0f, 45.0f, 0.0f), 0.001f)
                && Snapshot.Transform.Scale.Equals(Pico::FVector3(1.5f, 1.0f, 1.0f)),
            "Loaded object restores reflected values");
        Runner.Expect(
            Snapshot.HealthSeenInPostLoad == 75
                && Snapshot.TransformSeenInPostLoad.Equals(Snapshot.Transform, 0.001f),
            "PostLoad observes restored scalar and transform state");
        Pico::FTransform ReflectedTransform;
        Runner.Expect(
            TransformProperty->GetValue(Session.GetObject(), ReflectedTransform)
                && ReflectedTransform.Equals(Snapshot.Transform, 0.001f),
            "Transform property performs type-checked reflected reads");
        Runner.Expect(
            !TransformProperty->SetValue(Session.GetObject(), Pico::FVector3::ZeroVector),
            "Transform property rejects a mismatched reflected value type");
    }

    {
        std::unique_ptr<Pico::IGameModule> Module =
            PicoSandbox::CreateSandboxGameModule();
        Pico::FGameEngine GameEngine(Module.get());
        char ProgramName[] = "PicoSandboxTests";
        char* Arguments[] = { ProgramName };
        const std::filesystem::path ProjectFile =
            std::filesystem::absolute("PicoSandbox.pico");

        Runner.Expect(
            GameEngine.PreInit(1, Arguments, ProjectFile) == 0,
            "Sandbox game engine pre-initializes the project");
        Runner.Expect(
            GameEngine.Init() == 0,
            "Sandbox module starts before the default map is loaded");
        Runner.Expect(
            Pico::FClassRegistry::FindClass(Pico::FName("PSandboxPawn"))
                == PicoSandbox::PSandboxPawn::StaticClass()
                && PicoSandbox::PSandboxPawn::StaticClass()->IsChildOf(
                    Pico::PPawn::StaticClass())
                && Pico::FClassRegistry::FindClass(Pico::FName("PSandboxGameMode"))
                    == PicoSandbox::PSandboxGameMode::StaticClass(),
            "Sandbox runtime registers project Pawn and GameMode classes");

        auto* GameInstance = dynamic_cast<PicoSandbox::PSandboxGameInstance*>(
            GameEngine.GetGameInstance());
        Pico::PGameModeBase* GameMode = GameEngine.GetEngineLoop().GetWorld() != nullptr
            ? GameEngine.GetEngineLoop().GetWorld()->GetGameMode()
            : nullptr;
        Pico::PLocalPlayer* LocalPlayer = GameInstance != nullptr
            ? GameInstance->GetPrimaryLocalPlayer()
            : nullptr;
        auto* Controller = LocalPlayer != nullptr
                && LocalPlayer->GetPlayerController() != nullptr
                && LocalPlayer->GetPlayerController()->IsA(
                    PicoSandbox::PSandboxPlayerController::StaticClass())
            ? static_cast<PicoSandbox::PSandboxPlayerController*>(
                LocalPlayer->GetPlayerController())
            : nullptr;
        PicoSandbox::PSandboxPawn* Pawn = Controller != nullptr
                && Controller->GetPawn() != nullptr
                && Controller->GetPawn()->IsA(
                    PicoSandbox::PSandboxPawn::StaticClass())
            ? static_cast<PicoSandbox::PSandboxPawn*>(Controller->GetPawn())
            : nullptr;
        Runner.Expect(
            Pawn != nullptr
                && Pawn->GetRootComponent() != nullptr
                && LocalPlayer != nullptr
                && Controller != nullptr
                && Controller->GetPlayer() == LocalPlayer
                && Pawn->GetController() == Controller
                && GameMode != nullptr
                && GameMode->GetClass()
                    == PicoSandbox::PSandboxGameMode::StaticClass()
                && GameMode->GetDefaultPawnClass()
                    == PicoSandbox::PSandboxPawn::StaticClass()
                && GameMode->GetPlayerControllerClass()
                    == PicoSandbox::PSandboxPlayerController::StaticClass(),
            "Sandbox logs in its LocalPlayer and uses GameMode defaults to spawn and possess the project Pawn");

        Pico::PObject* MeshObject = Pawn != nullptr
            ? Pico::FindObject(Pawn, Pico::FName("SandboxPlayerMesh"))
            : nullptr;
        auto* Mesh = MeshObject != nullptr
                && MeshObject->IsA(Pico::PStaticMeshComponent::StaticClass())
            ? static_cast<Pico::PStaticMeshComponent*>(MeshObject)
            : nullptr;
        Runner.Expect(
            PicoSandbox::PSandboxPawn::StaticClass()->GetDefaultSubobjects().size() == 2
                && Pawn != nullptr
                && Pawn->GetComponents().size() == 2
                && Pawn->GetRootComponent() != nullptr
                && Pawn->GetRootComponent()->GetName()
                    == Pico::FName("DefaultSceneRoot")
                && Mesh != nullptr
                && Mesh->GetAttachParent() == Pawn->GetRootComponent()
                && Pico::HasAnyFlags(
                    Mesh->GetFlags(), Pico::EObjectFlags::DefaultSubobject),
            "Sandbox Pawn materializes its root and mesh from class default-subobject templates");

        if (Pawn != nullptr)
        {
            const Pico::FVector3 StartLocation = Pawn->GetActorLocation();
            Pico::FInputSystem& Input = GameEngine.GetInputSystem();
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::W, true);
            GameEngine.Tick();
            Input.EndFrame();
            Runner.Expect(
                !Pawn->GetActorLocation().Equals(StartLocation),
                "Sandbox PlayerController consumes mapped input and drives its possessed Pawn");

            const Pico::FObjectHandle OldPawnHandle = Pawn->GetHandle();
            Runner.Expect(
                GameMode->RestartPlayer(Controller)
                    && Controller->GetPawn() != nullptr
                    && Controller->GetPawn()->GetHandle() != OldPawnHandle,
                "Sandbox GameMode respawns and re-possesses a new Pawn without replacing the Controller");
        }

        GameEngine.Exit();
        Runner.Expect(
            GameEngine.GetGameInstance() == nullptr,
            "GameInstance shuts down before the engine exits");
    }

    std::error_code FileError;
    std::filesystem::remove(TestPath, FileError);
    return Runner.Finish();
}
