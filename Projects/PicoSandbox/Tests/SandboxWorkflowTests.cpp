#include "TestRunner.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Property.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameModule.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/Engine/ActorBlueprint.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/ObjectGlobals.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxEntity.h"
#include "PicoSandbox/SandboxGameInstance.h"
#include "PicoSandbox/SandboxGameMode.h"
#include "PicoSandbox/SandboxModule.h"
#include "PicoSandbox/SandboxPawn.h"
#include "PicoSandbox/SandboxPlayerController.h"
#include "PicoSandbox/SandboxReplicationLabActor.h"
#include "PicoSandbox/SandboxSession.h"

#include <algorithm>
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

        const Pico::PClass* LabClass =
            PicoSandbox::PSandboxReplicationLabActor::StaticClass();
        const Pico::PProperty* InitialMarkerProperty =
            LabClass->FindProperty(Pico::FName("InitialSpawnMarker"));
        const Pico::PProperty* RevisionProperty =
            LabClass->FindProperty(Pico::FName("LabRevision"));
        Runner.Expect(
            Pico::FClassRegistry::FindClass(
                Pico::FName("PSandboxReplicationLabActor")) == LabClass
                && LabClass->GetDefaultObject() != nullptr
                && static_cast<const PicoSandbox::PSandboxReplicationLabActor*>(
                    LabClass->GetDefaultObject())->GetIsReplicated(),
            "Replication Lab registers a replicated Actor class and CDO");
        Runner.Expect(
            InitialMarkerProperty != nullptr
                && InitialMarkerProperty->HasAnyFlags(
                    Pico::EPropertyFlags::Replicated)
                && InitialMarkerProperty->GetMetadata().ReplicationCondition
                    == Pico::EReplicationCondition::InitialOnly
                && RevisionProperty != nullptr
                && RevisionProperty->HasAnyFlags(Pico::EPropertyFlags::Replicated)
                && RevisionProperty->GetMetadata().RepNotifyFunction
                    == Pico::FName("OnRep_LabRevision"),
            "Replication Lab exposes InitialOnly and RepNotify metadata generated by PHT");

        PicoSandbox::PSandboxReplicationLabActor* LabActor = nullptr;
        Pico::PWorld* RuntimeWorld = GameEngine.GetEngineLoop().GetWorld();
        if (RuntimeWorld != nullptr)
        {
            for (Pico::PLevel* Level : RuntimeWorld->GetLevels())
            {
                if (Level == nullptr) continue;
                for (Pico::PActor* Actor : Level->GetActors())
                {
                    if (Actor != nullptr && Actor->IsA(LabClass))
                    {
                        LabActor = static_cast<
                            PicoSandbox::PSandboxReplicationLabActor*>(Actor);
                        break;
                    }
                }
                if (LabActor != nullptr) break;
            }
        }
        Pico::PObject* LabCubeObject = LabActor != nullptr
            ? Pico::FindObject(LabActor, Pico::FName("ReplicationLabCube"))
            : nullptr;
        Runner.Expect(
            LabActor != nullptr
                && LabActor->GetInitialSpawnMarker() == 6202
                && LabActor->GetActorLocation().Equals(
                    Pico::FVector3(180.0f, 0.0f, 130.0f))
                && LabCubeObject != nullptr
                && LabCubeObject->IsA(Pico::PCubeComponent::StaticClass()),
            "Standalone authority automatically spawns the visible Replication Lab Actor");
        if (LabActor != nullptr && LabCubeObject != nullptr)
        {
            const Pico::FVector3 PreviousColor =
                static_cast<Pico::PCubeComponent*>(LabCubeObject)->GetColor();
            LabActor->AdvanceRevision();
            Runner.Expect(
                LabActor->GetLabRevision() == 1
                    && !static_cast<Pico::PCubeComponent*>(LabCubeObject)
                        ->GetColor().Equals(PreviousColor),
                "Changing the replicated lab state updates its visible color");
        }
        Pico::FAssetPath KnightBlueprintPath;
        Pico::FAssetPath::TryParse(
            "/Game/Characters/BP_Knight.pblueprint", KnightBlueprintPath);
        Pico::FAssetPath KnightProfilePath;
        Pico::FAssetPath::TryParse(
            "/Game/Characters/Knight_Male/Knight_Male.pcharprofile",
            KnightProfilePath);
        const Pico::PClass* KnightBlueprintClass =
            Pico::FindActorBlueprintGeneratedClass(KnightBlueprintPath);
        Runner.Expect(
            KnightBlueprintClass != nullptr
                && KnightBlueprintClass->IsChildOf(
                    PicoSandbox::PSandboxPawn::StaticClass())
                && KnightBlueprintClass->GetDefaultObject() != nullptr,
            "Actor Blueprint compiles a reflected generated class with its own CDO");
        const Pico::FDefaultSubobjectRecord* KnightMeshTemplate = nullptr;
        if (KnightBlueprintClass != nullptr)
        {
            for (const Pico::FDefaultSubobjectRecord& Record
                 : KnightBlueprintClass->GetDefaultSubobjects())
            {
                if (Record.Name == Pico::FName("SandboxAnimatedMesh"))
                {
                    KnightMeshTemplate = &Record;
                    break;
                }
            }
        }
        Runner.Expect(
            KnightMeshTemplate != nullptr
                && KnightMeshTemplate->Template != nullptr
                && static_cast<Pico::PSkeletalMeshComponent*>(
                    KnightMeshTemplate->Template.get())
                    ->GetCharacterProfileAsset() == KnightProfilePath,
            "Actor Blueprint applies reflected component overrides to generated defaults");
        Pico::PWorld* BlueprintWorld = GameEngine.GetEngineLoop().GetWorld();
        Pico::FActorSpawnParameters BlueprintSpawn;
        BlueprintSpawn.Name = Pico::FName("BlueprintKnightTest");
        Pico::PActor* BlueprintKnight = BlueprintWorld != nullptr
            && KnightBlueprintClass != nullptr
            ? BlueprintWorld->SpawnActor(KnightBlueprintClass, BlueprintSpawn)
            : nullptr;
        Pico::PObject* BlueprintMesh = BlueprintKnight != nullptr
            ? Pico::FindObject(
                BlueprintKnight, Pico::FName("SandboxAnimatedMesh"))
            : nullptr;
        Runner.Expect(
            BlueprintKnight != nullptr
                && BlueprintKnight->GetClass() == KnightBlueprintClass
                && BlueprintMesh != nullptr
                && BlueprintMesh->IsA(Pico::PSkeletalMeshComponent::StaticClass())
                && static_cast<Pico::PSkeletalMeshComponent*>(BlueprintMesh)
                    ->GetCharacterProfileAsset() == KnightProfilePath,
            "SpawnActor materializes the generated Blueprint CDO and component templates");
        Pico::FWorldAssetData BlueprintWorldData;
        const bool bCapturedBlueprintClass = BlueprintWorld != nullptr
            && Pico::CaptureWorld(*BlueprintWorld, BlueprintWorldData)
            && std::any_of(
                BlueprintWorldData.Objects.begin(),
                BlueprintWorldData.Objects.end(),
                [KnightBlueprintClass](const Pico::FSceneObjectRecord& Record)
                {
                    return KnightBlueprintClass != nullptr
                        && Record.ObjectName == "BlueprintKnightTest"
                        && Record.ClassName
                            == KnightBlueprintClass->GetName().ToString();
                });
        Runner.Expect(
            bCapturedBlueprintClass,
            "World serialization records the stable generated Blueprint class identity");
        if (BlueprintKnight != nullptr) BlueprintWorld->DestroyActor(BlueprintKnight);
        Runner.Expect(
            Pico::FClassRegistry::FindClass(Pico::FName("PSandboxPawn"))
                == PicoSandbox::PSandboxPawn::StaticClass()
                && PicoSandbox::PSandboxPawn::StaticClass()->IsChildOf(
                    Pico::PCharacter::StaticClass())
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
                    == KnightBlueprintClass
                && GameMode->GetPlayerControllerClass()
                    == PicoSandbox::PSandboxPlayerController::StaticClass(),
            "Sandbox logs in its LocalPlayer and resolves the generated Blueprint as its default Pawn class");

        auto* Movement = Pawn != nullptr
                && Pawn->GetMovementComponent() != nullptr
                && Pawn->GetMovementComponent()->IsA(
                    Pico::PCharacterMovementComponent::StaticClass())
            ? static_cast<Pico::PCharacterMovementComponent*>(
                Pawn->GetMovementComponent())
            : nullptr;
        Pico::PObject* AnimatedMeshObject = Pawn != nullptr
            ? Pico::FindObject(Pawn, Pico::FName("SandboxAnimatedMesh"))
            : nullptr;
        auto* AnimatedMesh = AnimatedMeshObject != nullptr
                && AnimatedMeshObject->IsA(Pico::PSkeletalMeshComponent::StaticClass())
            ? static_cast<Pico::PSkeletalMeshComponent*>(AnimatedMeshObject)
            : nullptr;
        Pico::PObject* CameraBoomObject = Pawn != nullptr
            ? Pico::FindObject(Pawn, Pico::FName("CameraBoom")) : nullptr;
        auto* CameraBoom = CameraBoomObject != nullptr
                && CameraBoomObject->IsA(Pico::PSpringArmComponent::StaticClass())
            ? static_cast<Pico::PSpringArmComponent*>(CameraBoomObject) : nullptr;
        Pico::PObject* FollowCameraObject = Pawn != nullptr
            ? Pico::FindObject(Pawn, Pico::FName("FollowCamera")) : nullptr;
        auto* FollowCamera = FollowCameraObject != nullptr
                && FollowCameraObject->IsA(Pico::PCameraComponent::StaticClass())
            ? static_cast<Pico::PCameraComponent*>(FollowCameraObject) : nullptr;
        const bool bBaseDefaultsValid =
            PicoSandbox::PSandboxPawn::StaticClass()->GetDefaultSubobjects().size() == 6
                && Pawn != nullptr
                && Pawn->GetComponents().size() == 6
                && Pawn->GetRootComponent() != nullptr
                && Pawn->GetRootComponent()->IsA(Pico::PCapsuleComponent::StaticClass())
                && Pawn->GetRootComponent()->GetName()
                    == Pico::FName("CollisionCapsule")
                && static_cast<Pico::PCapsuleComponent*>(Pawn->GetRootComponent())
                    ->GetCollisionEnabled() == Pico::ECollisionEnabled::QueryOnly
                && static_cast<Pico::PCapsuleComponent*>(Pawn->GetRootComponent())
                    ->GetPhysicsBodyType() == Pico::EPhysicsBodyType::Kinematic
                && Movement != nullptr
                && AnimatedMesh != nullptr
                && AnimatedMesh->GetAttachParent() == Pawn->GetRootComponent()
                && CameraBoom != nullptr
                && FollowCamera != nullptr
                && Pico::HasAnyFlags(
                    AnimatedMesh->GetFlags(), Pico::EObjectFlags::DefaultSubobject);
        Runner.Expect(
            bBaseDefaultsValid,
            "Sandbox Pawn materializes collision, skeletal mesh, Movement, SpringArm, and Camera defaults");
        Runner.Expect(
            AnimatedMesh != nullptr
                && AnimatedMesh->GetAttachParent() == Pawn->GetRootComponent()
                && Pico::HasAnyFlags(
                    AnimatedMesh->GetFlags(), Pico::EObjectFlags::DefaultSubobject),
            "Sandbox Pawn materializes its animated mesh default subobject");
        Runner.Expect(
            AnimatedMesh != nullptr
                && AnimatedMesh->GetCharacterProfileAsset().IsValid(),
            "Sandbox GameMode configures the Character Profile before World BeginPlay");

        if (Pawn != nullptr)
        {
            const Pico::FVector3 StartLocation = Pawn->GetActorLocation();
            Pico::FInputSystem& Input = GameEngine.GetInputSystem();
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::W, true);
            GameEngine.GetEngineLoop().GetWorld()->Tick(0.1f);
            Input.EndFrame();
            GameEngine.GetEngineLoop().GetWorld()->Tick(0.1f);
            Runner.Expect(
                AnimatedMesh != nullptr
                    && AnimatedMesh->GetAnimInstance() != nullptr
                    && !AnimatedMesh->GetRenderData().Vertices.empty()
                    && AnimatedMesh->GetAnimationState() == Pico::EAnimationState::Walk,
                "World BeginPlay loads the Profile, creates AnimInstance, and selects Walk animation");
            Runner.Expect(
                AnimatedMesh != nullptr
                    && AnimatedMesh->UsesCharacterProfileVisualTransform()
                    && AnimatedMesh->GetCharacterProfileVisualTransform().Translation.Equals(
                        Pico::FVector3(0.0f, 0.0f, -96.0f), 0.01f)
                    && AnimatedMesh->GetCharacterProfileVisualTransform().Rotation.Rotator().Equals(
                        Pico::FRotator(0.0f, 180.0f, 0.0f), 0.01f)
                    && AnimatedMesh->GetVisualWorldTransform().Equals(
                        AnimatedMesh->GetCharacterProfileVisualTransform()
                            * AnimatedMesh->GetWorldTransform(),
                        0.01f),
                "Character Profile composes its asset visual transform without overwriting the component transform");
            Pico::FAssetPath SlotFiveOverride;
            Pico::FAssetPath::TryParse(
                "/Game/Characters/Knight_Male/Materials/Red.pmat", SlotFiveOverride);
            AnimatedMesh->SetMaterialOverride(5, SlotFiveOverride);
            AnimatedMesh->SetCharacterProfileAsset(
                AnimatedMesh->GetCharacterProfileAsset());
            GameEngine.GetEngineLoop().GetWorld()->Tick(0.016f);
            Runner.Expect(
                AnimatedMesh->GetMaterialOverride(5) == SlotFiveOverride,
                "Instance material slot 5 remains authoritative after the Character Profile reloads");
            Runner.Expect(
                Pawn->GetPendingMovementInputVector().IsNearlyZero()
                    && !Pawn->GetLastMovementInputVector().IsNearlyZero(),
                "Sandbox MovementComponent consumes PlayerController input");
            Runner.Expect(
                Movement != nullptr
                    && Movement->GetUpdatedComponent() == Pawn->GetRootComponent()
                    && Movement->PrimaryComponentTick.HasPrerequisite(
                        Controller->PrimaryActorTick),
                "Sandbox MovementComponent ticks after its possessing Controller");
            Runner.Expect(
                Movement != nullptr
                    && !Movement->GetVelocity().IsNearlyZero(),
                "Sandbox movement input produces velocity");
            Runner.Expect(
                Movement != nullptr
                    && Movement->ShouldOrientRotationToMovement()
                    && !Movement->UsesControllerDesiredRotation()
                    && !Pawn->UsesControllerRotationYaw()
                    && Pawn->GetMovementReference()
                        == PicoSandbox::EMovementReference::ControlRotation,
                "Sandbox defaults match the UE third-person free-look control policy");

            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::W, false);
            Controller->SetControlRotation(Pico::FRotator(-15.0f, 25.0f, 0.0f));
            Pawn->SetActorRotation(Pico::FRotator(0.0f, 25.0f, 0.0f));
            const Pico::FVector3 CameraForwardBeforeReverse =
                FollowCamera != nullptr
                    ? FollowCamera->GetViewForward() : Pico::FVector3::ZeroVector;
            Input.SetKeyState(Pico::EKey::S, true);
            GameEngine.GetEngineLoop().GetWorld()->Tick(0.1f);
            Input.EndFrame();
            Runner.Expect(
                FollowCamera != nullptr
                    && CameraBoom != nullptr
                    && CameraBoom->GetTargetRotation().Equals(
                        Controller->GetControlRotation(), 0.001f)
                    && FollowCamera->GetViewForward().Equals(
                        CameraForwardBeforeReverse, 0.001f)
                    && std::abs(Pico::FRotator::NormalizeAxis(
                        Pawn->GetActorRotation().Yaw - 25.0f)) > 1.0f,
                "Pressing S turns the Character toward camera-relative reverse without rotating the camera");
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::S, false);
            Input.EndFrame();

            const Pico::FQuat HorizontalViewRotation = Pico::FQuat::FromRotator(
                Pico::FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
            const Pico::FVector3 ExpectedCameraRight = Pico::FVector3::Cross(
                HorizontalViewRotation.RotateVector(Pico::FVector3::ForwardVector),
                Pico::FVector3::UpVector).GetSafeNormal();
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::D, true);
            GameEngine.GetEngineLoop().GetWorld()->Tick(1.0f / 60.0f);
            Input.EndFrame();
            Runner.Expect(
                Pawn->GetLastMovementInputVector().Equals(
                    ExpectedCameraRight, 0.001f),
                "Pressing D produces the same screen-right basis used by the camera view");
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::D, false);
            Input.EndFrame();

            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::MouseRight, true);
            GameEngine.GetEngineLoop().GetWorld()->Tick(1.0f / 60.0f);
            Input.EndFrame();
            Runner.Expect(
                Controller->GetControlMode()
                        == PicoSandbox::ECharacterControlMode::Strafe
                    && Movement != nullptr
                    && !Movement->ShouldOrientRotationToMovement()
                    && Movement->UsesControllerDesiredRotation(),
                "Aim switches the runtime policy to controller-facing Strafe mode");
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::MouseRight, false);
            Input.EndFrame();
            Runner.Expect(
                Movement != nullptr
                    && Movement->GetMovementMode()
                        == Pico::EMovementMode::Walking
                    && Movement->GetCurrentFloor().bWalkableFloor,
                "Sandbox CharacterMovement finds the floor and enters Walking");
            Runner.Expect(
                Movement != nullptr
                    && !Movement->GetLastHitResult().bStartPenetrating,
                "Sandbox Pawn starts outside blocking physics geometry");
            Runner.Expect(
                !Pawn->GetActorLocation().Equals(StartLocation),
                "Sandbox movement advances the possessed Pawn through the active collision scene");

            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::W, false);
            Input.SetKeyState(Pico::EKey::Space, true);
            GameEngine.GetEngineLoop().GetWorld()->Tick(1.0f / 60.0f);
            Input.EndFrame();
            Runner.Expect(
                Movement != nullptr
                    && Movement->GetMovementMode()
                        == Pico::EMovementMode::Falling
                    && Movement->GetVelocity().Z > 0.0f,
                "Sandbox Jump action starts an upward Falling move");
            Input.BeginFrame();
            Input.SetKeyState(Pico::EKey::Space, false);
            Input.EndFrame();

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
