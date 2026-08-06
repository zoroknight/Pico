#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransformService.h"
#include "Pico/Editor/EditorTransactionManager.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Property.h"
#include "TestRunner.h"

#include <string>
#include <string_view>
#include <vector>

namespace
{
Pico::PObject* FindWorldObjectByPath(
    Pico::PWorld* World,
    std::string_view Path)
{
    if (World == nullptr || Path.empty())
    {
        return nullptr;
    }
    if (World->GetPathName() == Path)
    {
        return World;
    }
    for (Pico::PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        if (Level->GetPathName() == Path)
        {
            return Level;
        }
        for (Pico::PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr)
            {
                continue;
            }
            if (Actor->GetPathName() == Path)
            {
                return Actor;
            }
            for (Pico::PActorComponent* Component : Actor->GetComponents())
            {
                if (Component != nullptr && Component->GetPathName() == Path)
                {
                    return Component;
                }
            }
        }
    }
    return nullptr;
}

Pico::PActorComponent* FindComponentByName(
    Pico::PActor* Actor,
    std::string_view Name)
{
    if (Actor == nullptr)
    {
        return nullptr;
    }
    for (Pico::PActorComponent* Component : Actor->GetComponents())
    {
        if (Component != nullptr
            && Component->GetName().ToString() == Name)
        {
            return Component;
        }
    }
    return nullptr;
}

void TestEditorSceneClipboard(
    FTestRunner& Runner,
    Pico::FEngineLoop& EngineLoop)
{
    Pico::PWorld* World = EngineLoop.GetWorld();
    Pico::PActor* SourceActor =
        World != nullptr
        ? World->SpawnActor<Pico::PActor>("ClipboardActor")
        : nullptr;
    Pico::PCubeComponent* SourceRoot =
        SourceActor != nullptr
        ? SourceActor->CreateComponent<Pico::PCubeComponent>("RootCube")
        : nullptr;
    Pico::PCubeComponent* SourceChild =
        SourceActor != nullptr
        ? SourceActor->CreateComponent<Pico::PCubeComponent>("ChildCube")
        : nullptr;
    Pico::PCubeComponent* SourceGrandchild =
        SourceActor != nullptr
        ? SourceActor->CreateComponent<Pico::PCubeComponent>("GrandchildCube")
        : nullptr;
    const bool bSourceCreated =
        SourceActor != nullptr
        && SourceRoot != nullptr
        && SourceChild != nullptr
        && SourceGrandchild != nullptr
        && SourceActor->SetRootComponent(SourceRoot)
        && SourceChild->AttachToComponent(
            SourceRoot,
            Pico::EAttachmentTransformRule::KeepRelative)
        && SourceGrandchild->AttachToComponent(
            SourceChild,
            Pico::EAttachmentTransformRule::KeepRelative);
    if (SourceRoot != nullptr)
    {
        SourceRoot->SetExtent(Pico::FVector3(80.0f, 60.0f, 40.0f));
    }
    if (SourceChild != nullptr)
    {
        SourceChild->SetRelativeLocation(Pico::FVector3(25.0f, 50.0f, 75.0f));
        SourceChild->SetExtent(Pico::FVector3(12.0f, 24.0f, 36.0f));
    }
    if (SourceGrandchild != nullptr)
    {
        SourceGrandchild->SetRelativeLocation(Pico::FVector3(5.0f, 10.0f, 15.0f));
    }
    Runner.Expect(
        bSourceCreated,
        "Clipboard test creates an Actor with a two-level component subtree");

    const std::string SourceActorPath =
        SourceActor != nullptr ? SourceActor->GetPathName() : std::string {};
    Pico::FEditorSceneClipboard Clipboard;
    Pico::EEditorClipboardError ClipboardError =
        Pico::EEditorClipboardError::None;
    Runner.Expect(
        Clipboard.Copy(*World, SourceActorPath, &ClipboardError)
            && Clipboard.GetContentType()
                == Pico::EEditorClipboardContentType::Actor,
        "Copy captures an Actor and its complete component records");

    Pico::FWorldAssetData ActorPasteData;
    std::string PastedActorPath;
    Runner.Expect(
        Clipboard.BuildPaste(
            *World,
            SourceActorPath,
            ActorPasteData,
            PastedActorPath,
            &ClipboardError)
            && PastedActorPath
                == "GameWorld.PersistentLevel.ClipboardActor_Copy",
        "Actor paste allocates new IDs and a unique Actor name");

    Pico::FEditorTransactionManager Transactions;
    Pico::EWorldSerializationError WorldError =
        Pico::EWorldSerializationError::None;
    std::string RestoredSelectionPath;
    const auto Restore =
        [&EngineLoop, &RestoredSelectionPath](
            const Pico::FEditorWorldSnapshot& Snapshot,
            Pico::EWorldSerializationError* RestoreError)
        {
            if (!EngineLoop.ReplaceWorld(Snapshot.WorldData, RestoreError))
            {
                return false;
            }
            RestoredSelectionPath = Snapshot.PrimaryObjectPath;
            return true;
        };
    Runner.Expect(
        Transactions.Begin(
            "Paste Actor",
            *World,
            SourceActorPath,
            &WorldError)
            && EngineLoop.ReplaceWorld(ActorPasteData, &WorldError),
        "Actor paste replaces the live World inside one transaction");
    World = EngineLoop.GetWorld();
    Pico::PActor* PastedActor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(World, PastedActorPath));
    Runner.Expect(
        PastedActor != nullptr
            && PastedActor->GetComponents().size() == 3
            && PastedActor->GetRootComponent() != nullptr
            && PastedActor->GetRootComponent()->GetName().ToString()
                == "RootCube"
            && Transactions.Commit(*World, PastedActorPath, &WorldError),
        "Actor paste preserves components and root relation");

    Pico::PCubeComponent* PastedChild = static_cast<Pico::PCubeComponent*>(
        FindComponentByName(PastedActor, "ChildCube"));
    Pico::PCubeComponent* PastedGrandchild =
        static_cast<Pico::PCubeComponent*>(
            FindComponentByName(PastedActor, "GrandchildCube"));
    Runner.Expect(
        PastedChild != nullptr
            && PastedGrandchild != nullptr
            && PastedChild->GetAttachParent()
                == PastedActor->GetRootComponent()
            && PastedGrandchild->GetAttachParent() == PastedChild
            && PastedChild->GetRelativeLocation().Equals(
                Pico::FVector3(25.0f, 50.0f, 75.0f))
            && PastedChild->GetExtent().Equals(
                Pico::FVector3(12.0f, 24.0f, 36.0f)),
        "Actor paste preserves attachment hierarchy and reflected properties");

    std::string Description;
    Runner.Expect(
        Transactions.Undo(Restore, &Description, &WorldError)
            && FindWorldObjectByPath(
                EngineLoop.GetWorld(),
                PastedActorPath) == nullptr,
        "Undo removes the complete pasted Actor");
    Runner.Expect(
        Transactions.Redo(Restore, &Description, &WorldError)
            && FindWorldObjectByPath(
                EngineLoop.GetWorld(),
                PastedActorPath) != nullptr,
        "Redo reconstructs the complete pasted Actor");

    World = EngineLoop.GetWorld();
    Pico::FWorldAssetData RepeatedPasteData;
    std::string RepeatedPastePath;
    Runner.Expect(
        Clipboard.BuildPaste(
            *World,
            PastedActorPath,
            RepeatedPasteData,
            RepeatedPastePath,
            &ClipboardError)
            && RepeatedPastePath
                == "GameWorld.PersistentLevel.ClipboardActor_Copy_2",
        "Repeated Actor paste advances the unique copy suffix");

    SourceActor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(World, SourceActorPath));
    SourceRoot = static_cast<Pico::PCubeComponent*>(
        FindComponentByName(SourceActor, "RootCube"));
    SourceChild = static_cast<Pico::PCubeComponent*>(
        FindComponentByName(SourceActor, "ChildCube"));
    const std::string SourceChildPath =
        SourceChild != nullptr ? SourceChild->GetPathName() : std::string {};
    const std::string SourceRootPath =
        SourceRoot != nullptr ? SourceRoot->GetPathName() : std::string {};
    Runner.Expect(
        Clipboard.Copy(*World, SourceChildPath, &ClipboardError)
            && Clipboard.GetContentType()
                == Pico::EEditorClipboardContentType::SceneComponent,
        "Copy captures a SceneComponent attachment subtree");

    Pico::FWorldAssetData ComponentPasteData;
    std::string PastedComponentPath;
    Runner.Expect(
        Clipboard.BuildPaste(
            *World,
            SourceRootPath,
            ComponentPasteData,
            PastedComponentPath,
            &ClipboardError)
            && PastedComponentPath
                == SourceActorPath + ".ChildCube_Copy"
            && EngineLoop.ReplaceWorld(ComponentPasteData, &WorldError),
        "Component paste targets the selected parent and remaps subtree IDs");

    World = EngineLoop.GetWorld();
    SourceActor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(World, SourceActorPath));
    SourceRoot = static_cast<Pico::PCubeComponent*>(
        FindComponentByName(SourceActor, "RootCube"));
    Pico::PCubeComponent* PastedComponent =
        static_cast<Pico::PCubeComponent*>(
            FindWorldObjectByPath(World, PastedComponentPath));
    Pico::PCubeComponent* PastedComponentChild =
        static_cast<Pico::PCubeComponent*>(
            FindComponentByName(SourceActor, "GrandchildCube_Copy"));
    Runner.Expect(
        PastedComponent != nullptr
            && PastedComponentChild != nullptr
            && PastedComponent->GetAttachParent() == SourceRoot
            && PastedComponentChild->GetAttachParent() == PastedComponent
            && PastedComponent->GetExtent().Equals(
                Pico::FVector3(12.0f, 24.0f, 36.0f)),
        "Component paste preserves its subtree, destination attachment, and properties");

    Pico::PActor* EmptyActor =
        World->SpawnActor<Pico::PActor>("EmptyClipboardTarget");
    const std::string EmptyActorPath =
        EmptyActor != nullptr ? EmptyActor->GetPathName() : std::string {};
    Pico::FWorldAssetData RootPasteData;
    std::string PastedRootPath;
    Runner.Expect(
        EmptyActor != nullptr
            && Clipboard.BuildPaste(
                *World,
                EmptyActorPath,
                RootPasteData,
                PastedRootPath,
                &ClipboardError)
            && PastedRootPath == EmptyActorPath + ".ChildCube"
            && EngineLoop.ReplaceWorld(RootPasteData, &WorldError),
        "Pasting a component subtree into an empty Actor builds valid World data");
    EmptyActor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(EngineLoop.GetWorld(), EmptyActorPath));
    Runner.Expect(
        EmptyActor != nullptr
            && EmptyActor->GetRootComponent() != nullptr
            && EmptyActor->GetRootComponent()->GetPathName()
                == PastedRootPath
            && EmptyActor->GetRootComponent()->GetAttachChildren().size() == 1,
        "A pasted component becomes the root when the destination Actor has no root");

    Transactions.Clear();
}

void TestEditorCommandService(FTestRunner& Runner)
{
    char Program[] = "PicoEditorCommandTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, MaxFPS };

    Pico::FEngineLoop EngineLoop;
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments) == 0 && EngineLoop.Init() == 0,
        "Editor command service initializes without an ImGui context");

    Pico::FEditorSelection Selection;
    Selection.Set(EngineLoop.GetWorld());
    Pico::FEditorTransactionManager Transactions;
    Pico::FEditorSceneClipboard Clipboard;
    Pico::FEditorCommandService Commands(
        &EngineLoop,
        &Selection,
        &Transactions,
        &Clipboard);

    const Pico::FEditorCommandResult SpawnResult = Commands.SpawnActor(true);
    Pico::PObject* SpawnedObject = Selection.Resolve();
    const std::string SpawnedPath =
        SpawnedObject != nullptr ? SpawnedObject->GetPathName() : std::string {};
    Runner.Expect(
        SpawnResult.bSucceeded
            && SpawnedObject != nullptr
            && SpawnedObject->IsA(Pico::PActor::StaticClass())
            && Transactions.CanUndo(),
        "Command service spawns a transactional Cube Actor and selects it");

    Runner.Expect(
        Commands.AddComponent(false).bSucceeded
            && Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PSceneComponent::StaticClass()),
        "Command service adds and selects a SceneComponent");

    Selection.Restore(EngineLoop.GetWorld(), SpawnedPath);
    const Pico::FObjectHandle ActorHandle = Selection.GetHandle();
    Runner.Expect(
        Commands.RenameObject(ActorHandle, "CommandActor").bSucceeded
            && Selection.GetObjectPath().ends_with(".CommandActor"),
        "Command service validates and transactionally renames an Actor");
    Runner.Expect(
        !Commands.RenameObject(Selection.GetHandle(), "Invalid.Name").bSucceeded,
        "Command service rejects names that would make object paths ambiguous");

    Runner.Expect(
        Commands.CopySelectedObject().bSucceeded
            && Commands.PasteClipboard().bSucceeded
            && Selection.GetObjectPath().find("CommandActor_Copy") != std::string::npos,
        "Command service copies and pastes through the shared editor clipboard");
    const std::string PastedPath = Selection.GetObjectPath();
    Runner.Expect(
        Commands.DeleteSelectedObject().bSucceeded
            && Pico::FindEditorWorldObjectByPath(EngineLoop.GetWorld(), PastedPath) == nullptr,
        "Command service deletes the selected pasted Actor");
    Runner.Expect(
        Commands.Undo().bSucceeded
            && Pico::FindEditorWorldObjectByPath(EngineLoop.GetWorld(), PastedPath) != nullptr
            && Commands.Redo().bSucceeded
            && Pico::FindEditorWorldObjectByPath(EngineLoop.GetWorld(), PastedPath) == nullptr,
        "Command service Undo and Redo restore complete World snapshots");

    Runner.Expect(
        Commands.SpawnActor(true).bSucceeded,
        "Command service creates the first Actor for multi-selection");
    Pico::PObject* FirstSelectedActor = Selection.Resolve();
    const std::string FirstSelectedPath = Selection.GetObjectPath();
    Runner.Expect(
        Commands.SpawnActor(true).bSucceeded,
        "Command service creates the second Actor for multi-selection");
    Pico::PObject* SecondSelectedActor = Selection.Resolve();
    const std::string SecondSelectedPath = Selection.GetObjectPath();
    const std::vector<Pico::PObject*> OrderedActors {
        FirstSelectedActor,
        SecondSelectedActor
    };
    Selection.Set(FirstSelectedActor);
    Runner.Expect(
        Selection.SetRange(OrderedActors, SecondSelectedActor, false)
            && Selection.Num() == 2
            && Selection.Contains(FirstSelectedActor)
            && Selection.Contains(SecondSelectedActor)
            && Selection.Resolve() == SecondSelectedActor,
        "Editor selection supports anchored range selection with a primary object");
    Runner.Expect(
        Selection.Toggle(FirstSelectedActor)
            && Selection.Num() == 1
            && !Selection.Contains(FirstSelectedActor)
            && Selection.Contains(SecondSelectedActor)
            && Selection.Add(FirstSelectedActor)
            && Selection.Num() == 2,
        "Editor selection supports Ctrl-style toggle and additive selection");
    Runner.Expect(
        Commands.CopySelectedObject().bSucceeded
            && Commands.PasteClipboard().bSucceeded
            && Selection.Num() == 2
            && Selection.GetObjectPaths()[0] != FirstSelectedPath
            && Selection.GetObjectPaths()[1] != SecondSelectedPath
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), FirstSelectedPath) != nullptr
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), SecondSelectedPath) != nullptr,
        "Command service copies and pastes multiple selected Actors");
    Pico::PObject* RestoredFirstActor = Pico::FindEditorWorldObjectByPath(
        EngineLoop.GetWorld(), FirstSelectedPath);
    Pico::PObject* RestoredSecondActor = Pico::FindEditorWorldObjectByPath(
        EngineLoop.GetWorld(), SecondSelectedPath);
    Selection.Set(RestoredFirstActor);
    Selection.Add(RestoredSecondActor);
    Runner.Expect(
        Commands.DeleteSelectedObject().bSucceeded
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), FirstSelectedPath) == nullptr
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), SecondSelectedPath) == nullptr,
        "Command service deletes multiple selected scene Actors in one command");
    Runner.Expect(
        Commands.Undo().bSucceeded
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), FirstSelectedPath) != nullptr
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), SecondSelectedPath) != nullptr
            && Selection.Num() == 2
            && Selection.GetObjectPaths().size() == 2,
        "Batch delete Undo restores all Actors and the complete selection set");
    Runner.Expect(
        Commands.Redo().bSucceeded
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), FirstSelectedPath) == nullptr
            && Pico::FindEditorWorldObjectByPath(
                EngineLoop.GetWorld(), SecondSelectedPath) == nullptr,
        "Batch delete Redo removes the complete multi-selection again");

    Pico::FAssetPath StaticMeshPath;
    Pico::FAssetPath::TryParse(
        "/Game/Meshes/PicoPyramid.pmesh",
        StaticMeshPath);
    const Pico::FEditorCommandResult SpawnStaticMesh =
        Commands.SpawnStaticMeshActor(StaticMeshPath);
    Pico::PActor* StaticMeshActor = Selection.Resolve() != nullptr
        && Selection.Resolve()->IsA(Pico::PActor::StaticClass())
        ? static_cast<Pico::PActor*>(Selection.Resolve())
        : nullptr;
    Pico::PStaticMeshComponent* StaticMeshRoot = StaticMeshActor != nullptr
        && StaticMeshActor->GetRootComponent() != nullptr
        && StaticMeshActor->GetRootComponent()->IsA(
            Pico::PStaticMeshComponent::StaticClass())
        ? static_cast<Pico::PStaticMeshComponent*>(
            StaticMeshActor->GetRootComponent())
        : nullptr;
    Runner.Expect(
        SpawnStaticMesh.bSucceeded
            && StaticMeshRoot != nullptr
            && StaticMeshRoot->GetStaticMeshAsset() == StaticMeshPath,
        "Command service transactionally spawns an asset-backed Static Mesh Actor");
    Runner.Expect(
        Commands.AddStaticMeshComponent(StaticMeshPath).bSucceeded
            && Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PStaticMeshComponent::StaticClass()),
        "Command service adds and selects an asset-backed Static Mesh Component");

    EngineLoop.Exit();
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Editor command service releases all reconstructed objects");
}

void TestEditorTransformService(
    FTestRunner& Runner,
    Pico::FEngineLoop& EngineLoop)
{
    Pico::PWorld* World = EngineLoop.GetWorld();
    Pico::PActor* FirstActor = World != nullptr
        ? World->SpawnActor<Pico::PActor>("TransformFirst") : nullptr;
    Pico::PActor* PrimaryActor = World != nullptr
        ? World->SpawnActor<Pico::PActor>("TransformPrimary") : nullptr;
    Pico::PCubeComponent* FirstRoot = FirstActor != nullptr
        ? FirstActor->CreateComponent<Pico::PCubeComponent>("Root") : nullptr;
    Pico::PCubeComponent* PrimaryRoot = PrimaryActor != nullptr
        ? PrimaryActor->CreateComponent<Pico::PCubeComponent>("Root") : nullptr;
    const bool bCreated = FirstActor != nullptr
        && PrimaryActor != nullptr
        && FirstRoot != nullptr
        && PrimaryRoot != nullptr
        && FirstActor->SetRootComponent(FirstRoot)
        && PrimaryActor->SetRootComponent(PrimaryRoot)
        && FirstActor->SetActorTransform(Pico::FTransform(Pico::FVector3::ZeroVector))
        && PrimaryActor->SetActorTransform(
            Pico::FTransform(Pico::FVector3(10.0f, 0.0f, 0.0f)));
    Runner.Expect(bCreated, "Transform service test actors are created");
    if (!bCreated)
    {
        return;
    }

    Pico::FEditorSelection Selection;
    Selection.Set(FirstActor);
    Selection.Add(PrimaryActor);
    Pico::FEditorTransformService Service;
    Pico::FTransform GizmoTransform;
    Runner.Expect(
        Service.GetGizmoTransform(Selection, GizmoTransform)
            && GizmoTransform.Translation.Equals(Pico::FVector3(10.0f, 0.0f, 0.0f)),
        "The primary selection supplies the multi-select pivot");

    Runner.Expect(
        Service.BeginManipulation(
            Selection,
            Pico::EEditorTransformMode::Translate,
            Pico::EEditorCoordinateSpace::World)
            && Service.GetTargetCount() == 2,
        "Multi-select translation begins with two independent targets");
    GizmoTransform.Translation += Pico::FVector3(5.0f, 6.0f, 7.0f);
    Runner.Expect(
        Service.ApplyGizmoTransform(GizmoTransform)
            && FirstActor->GetActorLocation().Equals(Pico::FVector3(5.0f, 6.0f, 7.0f))
            && PrimaryActor->GetActorLocation().Equals(Pico::FVector3(15.0f, 6.0f, 7.0f)),
        "Multi-select translation preserves offsets");
    Runner.Expect(
        Service.CancelManipulation()
            && FirstActor->GetActorLocation().Equals(Pico::FVector3::ZeroVector)
            && PrimaryActor->GetActorLocation().Equals(Pico::FVector3(10.0f, 0.0f, 0.0f)),
        "Cancelling a transform restores every initial transform");

    Service.GetGizmoTransform(Selection, GizmoTransform);
    const Pico::FQuat QuarterTurn = Pico::FQuat::FromAxisAngle(
        Pico::FVector3::UpVector,
        Pico::DegreesToRadians(90.0f));
    GizmoTransform.Rotation = QuarterTurn * GizmoTransform.Rotation;
    Runner.Expect(
        Service.BeginManipulation(
            Selection,
            Pico::EEditorTransformMode::Rotate,
            Pico::EEditorCoordinateSpace::World)
            && Service.ApplyGizmoTransform(GizmoTransform)
            && FirstActor->GetActorLocation().Equals(
                Pico::FVector3(10.0f, 0.0f, 0.0f)
                    + QuarterTurn.RotateVector(Pico::FVector3(-10.0f, 0.0f, 0.0f)))
            && PrimaryActor->GetActorLocation().Equals(Pico::FVector3(10.0f, 0.0f, 0.0f)),
        "Multi-select rotation orbits objects around the primary pivot");
    Service.CancelManipulation();

    Service.GetGizmoTransform(Selection, GizmoTransform);
    GizmoTransform.Scale = Pico::FVector3(2.0f, 2.0f, 2.0f);
    Runner.Expect(
        Service.BeginManipulation(
            Selection,
            Pico::EEditorTransformMode::Scale,
            Pico::EEditorCoordinateSpace::World)
            && Service.ApplyGizmoTransform(GizmoTransform)
            && FirstActor->GetActorLocation().Equals(Pico::FVector3(-10.0f, 0.0f, 0.0f))
            && FirstActor->GetActorTransform().Scale.Equals(Pico::FVector3(2.0f))
            && PrimaryActor->GetActorTransform().Scale.Equals(Pico::FVector3(2.0f)),
        "Multi-select scaling changes object spacing and individual scale");
    Service.CancelManipulation();

    Selection.Set(PrimaryActor);
    Selection.Add(PrimaryRoot);
    Runner.Expect(
        Service.BeginManipulation(
            Selection,
            Pico::EEditorTransformMode::Translate,
            Pico::EEditorCoordinateSpace::World)
            && Service.GetTargetCount() == 1,
        "Selecting an Actor and its root component does not transform it twice");
    Service.EndManipulation();
}

void TestEditorTransactions(FTestRunner& Runner)
{
    char Program[] = "PicoEditorTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, MaxFPS };

    Pico::FEngineLoop EngineLoop;
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments) == 0 && EngineLoop.Init() == 0,
        "Editor transaction test initializes the engine loop");

    Pico::FEditorTransactionManager Transactions(2);
    Pico::PWorld* World = EngineLoop.GetWorld();
    const std::string WorldPath =
        World != nullptr ? World->GetPathName() : std::string {};
    Pico::EWorldSerializationError Error =
        Pico::EWorldSerializationError::None;
    std::string RestoredSelectionPath;
    const auto Restore =
        [&EngineLoop, &RestoredSelectionPath](
            const Pico::FEditorWorldSnapshot& Snapshot,
            Pico::EWorldSerializationError* RestoreError)
        {
            if (!EngineLoop.ReplaceWorld(Snapshot.WorldData, RestoreError))
            {
                return false;
            }
            RestoredSelectionPath = Snapshot.PrimaryObjectPath;
            return true;
        };

    Runner.Expect(
        World != nullptr
            && Transactions.Begin("Create Actor", *World, WorldPath, &Error),
        "A transaction captures its before snapshot");
    Pico::PActor* Actor =
        World != nullptr ? World->SpawnActor<Pico::PActor>("UndoActor") : nullptr;
    Pico::PSceneComponent* Root = Actor != nullptr
        ? Actor->CreateComponent<Pico::PSceneComponent>("DefaultSceneRoot")
        : nullptr;
    const bool bCreated =
        Actor != nullptr
        && Root != nullptr
        && Actor->SetRootComponent(Root);
    const std::string ActorPath =
        Actor != nullptr ? Actor->GetPathName() : std::string {};
    Runner.Expect(
        bCreated
            && Transactions.Commit(*World, ActorPath, &Error)
            && Transactions.CanUndo()
            && !Transactions.CanRedo(),
        "Creating an Actor commits one undo entry");

    std::string Description;
    Runner.Expect(
        Transactions.Undo(Restore, &Description, &Error)
            && Description == "Create Actor"
            && RestoredSelectionPath == WorldPath
            && FindWorldObjectByPath(EngineLoop.GetWorld(), ActorPath) == nullptr
            && Transactions.CanRedo(),
        "Undo removes the created Actor and restores the previous selection path");
    Runner.Expect(
        Transactions.Redo(Restore, &Description, &Error)
            && Description == "Create Actor"
            && RestoredSelectionPath == ActorPath
            && FindWorldObjectByPath(EngineLoop.GetWorld(), ActorPath) != nullptr,
        "Redo reconstructs the created Actor and its selection path");

    World = EngineLoop.GetWorld();
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(World, ActorPath));
    Runner.Expect(
        Actor != nullptr
            && Transactions.Begin("Rename Actor", *World, ActorPath, &Error)
            && Pico::RenameObject(Actor, Pico::FName("RenamedActor")),
        "Rename starts from a captured Actor state");
    const std::string RenamedPath =
        Actor != nullptr ? Actor->GetPathName() : std::string {};
    Runner.Expect(
        Actor != nullptr
            && Transactions.Commit(*World, RenamedPath, &Error),
        "Rename commits its after snapshot");
    Runner.Expect(
        Transactions.Undo(Restore, &Description, &Error)
            && FindWorldObjectByPath(EngineLoop.GetWorld(), ActorPath) != nullptr
            && RestoredSelectionPath == ActorPath,
        "Undo restores the original Actor name");
    Runner.Expect(
        Transactions.Redo(Restore, &Description, &Error)
            && FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath) != nullptr
            && RestoredSelectionPath == RenamedPath,
        "Redo restores the new Actor name");

    World = EngineLoop.GetWorld();
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(World, RenamedPath));
    Runner.Expect(
        Actor != nullptr
            && Transactions.Begin("Delete Actor", *World, RenamedPath, &Error)
            && World->DestroyActor(Actor)
            && Transactions.Commit(*World, WorldPath, &Error),
        "Deleting an Actor commits the complete removed subtree");

    const std::size_t UndoCountBeforeFailure = Transactions.GetUndoCount();
    const auto FailRestore =
        [](const Pico::FEditorWorldSnapshot&,
           Pico::EWorldSerializationError* RestoreError)
        {
            if (RestoreError != nullptr)
            {
                *RestoreError = Pico::EWorldSerializationError::WorldReplacementFailed;
            }
            return false;
        };
    Runner.Expect(
        !Transactions.Undo(FailRestore, &Description, &Error)
            && Error == Pico::EWorldSerializationError::WorldReplacementFailed
            && Transactions.GetUndoCount() == UndoCountBeforeFailure,
        "A failed restore leaves transaction history unchanged");
    Runner.Expect(
        Transactions.Undo(Restore, &Description, &Error)
            && FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath) != nullptr
            && RestoredSelectionPath == RenamedPath,
        "Undo reconstructs a deleted Actor and its component subtree");
    Runner.Expect(
        Transactions.Redo(Restore, &Description, &Error)
            && FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath) == nullptr
            && RestoredSelectionPath == WorldPath,
        "Redo deletes the reconstructed Actor again");

    Runner.Expect(
        Transactions.Undo(Restore, &Description, &Error)
            && Transactions.CanRedo(),
        "Undo prepares a redo entry before a divergent edit");
    World = EngineLoop.GetWorld();
    Runner.Expect(
        Transactions.Begin("Create Different Actor", *World, WorldPath, &Error),
        "A divergent edit begins after undo");
    Pico::PActor* DifferentActor =
        World->SpawnActor<Pico::PActor>("DifferentActor");
    Runner.Expect(
        DifferentActor != nullptr
            && Transactions.Commit(
                *World,
                DifferentActor->GetPathName(),
                &Error)
            && !Transactions.CanRedo()
            && Transactions.GetUndoCount() <= 2,
        "A new commit clears redo and respects the history capacity");

    Transactions.Clear();
    World = EngineLoop.GetWorld();
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(World, RenamedPath));
    const Pico::FTransform OriginalActorTransform =
        Actor != nullptr
        ? Actor->GetActorTransform()
        : Pico::FTransform::Identity;
    const Pico::FTransform IntermediateActorTransform(
        Pico::FVector3(25.0f, 50.0f, 75.0f));
    const Pico::FTransform FinalActorTransform(
        Pico::FRotator(10.0f, 20.0f, 30.0f),
        Pico::FVector3(100.0f, 200.0f, 300.0f),
        Pico::FVector3(1.5f, 2.0f, 2.5f));
    Runner.Expect(
        Actor != nullptr
            && Transactions.Begin(
                "Edit Actor transform",
                *World,
                RenamedPath,
                &Error)
            && Actor->SetActorTransform(IntermediateActorTransform)
            && Actor->SetActorTransform(FinalActorTransform)
            && Transactions.Commit(*World, RenamedPath, &Error)
            && Transactions.GetUndoCount() == 1,
        "A continuous Transform edit commits one transaction");
    Runner.Expect(
        Transactions.Undo(Restore, &Description, &Error),
        "Transform Undo restores the before snapshot");
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath));
    Runner.Expect(
        Actor != nullptr
            && Actor->GetActorTransform().Equals(OriginalActorTransform),
        "Transform Undo restores the original Actor transform");
    Runner.Expect(
        Transactions.Redo(Restore, &Description, &Error),
        "Transform Redo restores the after snapshot");
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath));
    Runner.Expect(
        Actor != nullptr
            && Actor->GetActorTransform().Equals(FinalActorTransform),
        "Transform Redo restores the final value, not an intermediate drag value");

    World = EngineLoop.GetWorld();
    Pico::PSceneComponent* ReflectedRoot =
        Actor != nullptr ? Actor->GetRootComponent() : nullptr;
    const Pico::PProperty* RelativeTransformProperty =
        ReflectedRoot != nullptr
        ? ReflectedRoot->GetClass()->FindProperty(Pico::FName("RelativeTransform"))
        : nullptr;
    Pico::FTransform OriginalRelativeTransform;
    const Pico::FTransform EditedRelativeTransform(
        Pico::FRotator(5.0f, 15.0f, 25.0f),
        Pico::FVector3(-10.0f, 20.0f, 40.0f),
        Pico::FVector3(0.75f, 1.25f, 1.5f));
    Runner.Expect(
        RelativeTransformProperty != nullptr
            && RelativeTransformProperty->GetValue(
                ReflectedRoot,
                OriginalRelativeTransform)
            && Transactions.Begin(
                "Edit RelativeTransform",
                *World,
                ReflectedRoot->GetPathName(),
                &Error)
            && RelativeTransformProperty->SetValue(
                ReflectedRoot,
                EditedRelativeTransform)
            && Transactions.Commit(
                *World,
                ReflectedRoot->GetPathName(),
                &Error),
        "A reflected Transform property commits through the shared transaction backend");
    Runner.Expect(
        Transactions.Undo(Restore, &Description, &Error),
        "Reflected property Undo restores the before snapshot");
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath));
    ReflectedRoot = Actor != nullptr ? Actor->GetRootComponent() : nullptr;
    Runner.Expect(
        ReflectedRoot != nullptr
            && ReflectedRoot->GetRelativeTransform().Equals(
                OriginalRelativeTransform),
        "Reflected property Undo restores the serialized property value");
    Runner.Expect(
        Transactions.Redo(Restore, &Description, &Error),
        "Reflected property Redo restores the after snapshot");
    Actor = static_cast<Pico::PActor*>(
        FindWorldObjectByPath(EngineLoop.GetWorld(), RenamedPath));
    ReflectedRoot = Actor != nullptr ? Actor->GetRootComponent() : nullptr;
    Runner.Expect(
        ReflectedRoot != nullptr
            && ReflectedRoot->GetRelativeTransform().Equals(
                EditedRelativeTransform),
        "Reflected property Redo restores the edited property value");

    Transactions.Clear();
    Runner.Expect(
        !Transactions.CanUndo()
            && !Transactions.CanRedo()
            && !Transactions.HasPendingTransaction(),
        "Clearing transactions removes all editor history");

    TestEditorTransformService(Runner, EngineLoop);
    TestEditorSceneClipboard(Runner, EngineLoop);

    EngineLoop.Exit();
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Editor transaction tests release all reconstructed objects");
}
}

int main()
{
    FTestRunner Runner;
    TestEditorCommandService(Runner);
    TestEditorTransactions(Runner);
    return Runner.Finish();
}
