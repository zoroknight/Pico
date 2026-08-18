#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorProjectManager.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransformService.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Editor/EditorWorldDocument.h"
#include "Pico/Editor/PlaySession.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorBlueprint.h"
#include "Pico/Engine/CameraActor.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Property.h"
#include "Pico/Render/SceneViewportRenderer.h"
#include "PicoSandbox/SandboxModule.h"
#include "TestRunner.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
bool CopyEditorTestProject(
    const std::filesystem::path& SourceRoot,
    const std::filesystem::path& DestinationRoot)
{
    std::error_code Error;
    std::filesystem::remove_all(DestinationRoot, Error);
    Error.clear();
    std::filesystem::create_directories(DestinationRoot, Error);
    if (Error)
    {
        return false;
    }

    std::filesystem::copy_file(
        SourceRoot / "PicoSandbox.pico",
        DestinationRoot / "PicoSandbox.pico",
        std::filesystem::copy_options::overwrite_existing,
        Error);
    if (Error)
    {
        return false;
    }

    for (const std::filesystem::path& Directory : {
             std::filesystem::path("Config"),
             std::filesystem::path("Content") })
    {
        std::filesystem::copy(
            SourceRoot / Directory,
            DestinationRoot / Directory,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing,
            Error);
        if (Error)
        {
            return false;
        }
    }
    return true;
}

void TestEditorProjectManager(FTestRunner& Runner)
{
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path()
        / "PicoEditorProjectManagerTests";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    const std::filesystem::path ProjectFile = Root / "LearningProject.pico";
    {
        std::ofstream File(ProjectFile);
        File << "[Project]\nName=LearningProject\nFileVersion=1\n";
    }

    Pico::FEditorProjectResolution Resolution =
        Pico::ResolveEditorProjectPath(Root);
    Runner.Expect(
        Resolution.IsResolved()
            && Resolution.ProjectFile.filename() == ProjectFile.filename(),
        "A folder containing one .pico descriptor resolves to that project");

    const std::filesystem::path SecondProject = Root / "Second.pico";
    {
        std::ofstream File(SecondProject);
        File << "[Project]\nName=Second\nFileVersion=1\n";
    }
    Resolution = Pico::ResolveEditorProjectPath(Root);
    Runner.Expect(
        !Resolution.IsResolved() && Resolution.Candidates.size() == 2,
        "A folder containing multiple descriptors requires an explicit project file");

    const std::filesystem::path SettingsFile = Root / "EditorSettings.ini";
    Pico::FEditorProjectHistory History;
    History.Add(ProjectFile);
    History.Add(SecondProject);
    History.Add(ProjectFile);
    Runner.Expect(
        History.GetRecentProjects().size() == 2
            && History.GetRecentProjects().front().filename()
                == ProjectFile.filename()
            && History.Save(SettingsFile),
        "Recent project history de-duplicates and preserves most-recent order");
    Pico::FEditorProjectHistory LoadedHistory;
    Runner.Expect(
        LoadedHistory.Load(SettingsFile)
            && LoadedHistory.GetRecentProjects().size() == 2
            && LoadedHistory.GetRecentProjects().front().filename()
                == ProjectFile.filename(),
        "Recent project history round trips through user settings");

    Pico::FEditorSessionState Session;
    Pico::FAssetPath::TryParse(
        "/Game/Maps/StarterWorld.pworld", Session.LastWorld);
    Pico::FAssetPath::TryParse(
        "/Game/Characters/BP_Knight.pblueprint",
        Session.OpenActorBlueprint);
    const std::filesystem::path SessionFile = Root / "EditorSession.ini";
    Pico::FEditorSessionState LoadedSession;
    Runner.Expect(
        Session.Save(SessionFile)
            && LoadedSession.Load(SessionFile)
            && LoadedSession.LastWorld == Session.LastWorld
            && LoadedSession.OpenActorBlueprint == Session.OpenActorBlueprint,
        "Editor session restores the last World and open Actor Blueprint");

    std::filesystem::remove_all(Root, Error);
}

void TestViewportRenderOptionDefaults(FTestRunner& Runner)
{
    const Pico::FSceneViewportRenderOptions Options;
    Runner.Expect(
        Options.bDrawGrid
            && !Options.bDrawWorldAxes
            && Options.bDrawComponentVisualizations,
        "Viewport render options keep editor-only world axes opt-in");
}

void TestPlaySessionSettings(FTestRunner& Runner)
{
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / "PicoPlaySessionTests";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    const std::filesystem::path Executable = Root / "PicoGame.exe";
    const std::filesystem::path Project = Root / "LearningProject.pico";
    {
        std::ofstream File(Executable);
        File << "test";
    }
    {
        std::ofstream File(Project);
        File << "[Project]\nName=LearningProject\n";
    }

    Pico::FPlaySessionSettings Settings;
    Settings.NetMode = Pico::EEditorPlayNetMode::SeparateServer;
    Settings.PlayerCount = 2;
    Settings.ServerPort = 17777;
    Settings.ClientWindowWidth = 800;
    Settings.ClientWindowHeight = 450;
    const std::filesystem::path SettingsFile = Root / "PlaySettings.ini";
    Pico::FPlaySessionSettings Loaded;
    Runner.Expect(
        Settings.Save(SettingsFile)
            && Loaded.Load(SettingsFile)
            && Loaded.NetMode == Pico::EEditorPlayNetMode::SeparateServer
            && Loaded.PlayerCount == 2
            && Loaded.ServerPort == 17777
            && Loaded.ClientWindowWidth == 800
            && Loaded.ClientWindowHeight == 450,
        "Play Session settings persist mode, players, port, and window size");

    Pico::FPlaySessionLaunchRequest Request;
    Request.Settings = Loaded;
    Request.Executable = Executable;
    Request.ProjectFile = Project;
    Request.MapAssetPath = "/Game/Maps/Test.pworld";
    Request.WorkingDirectory = Root;
    Request.LogDirectory = Root / "Logs";
    std::vector<Pico::FPlayProcessSpec> Specs;
    std::string BuildError;
    const bool bBuilt = Pico::BuildPlayProcessSpecs(Request, Specs, BuildError);
    Runner.Expect(
        bBuilt && Specs.size() == 3
            && Specs[0].Role == Pico::EPlayProcessRole::Server
            && Specs[1].Role == Pico::EPlayProcessRole::Client
            && Specs[2].Role == Pico::EPlayProcessRole::Client,
        "Separate-server Play builds one server process and one process per player");
    Runner.Expect(
        bBuilt
            && std::find(Specs[0].Arguments.begin(), Specs[0].Arguments.end(),
                "-server") != Specs[0].Arguments.end()
            && std::find(Specs[1].Arguments.begin(), Specs[1].Arguments.end(),
                "-client=127.0.0.1") != Specs[1].Arguments.end()
            && Specs[0].LogFile.filename() == "Server.log"
            && Specs[1].LogFile.filename() == "Client_1.log",
        "Play process specs assign network roles and independent logs");

    Request.Settings.NetMode = Pico::EEditorPlayNetMode::Standalone;
    Request.Settings.PlayerCount = 1;
    Specs.clear();
    Runner.Expect(
        Pico::BuildPlayProcessSpecs(Request, Specs, BuildError)
            && Specs.size() == 1
            && Specs[0].Role == Pico::EPlayProcessRole::Standalone,
        "Standalone Play builds exactly one offline process");

    Request.Settings.NetMode = Pico::EEditorPlayNetMode::ListenServer;
    Runner.Expect(
        !Pico::BuildPlayProcessSpecs(Request, Specs, BuildError)
            && BuildError.find("replication") != std::string::npos,
        "Listen Server remains explicitly unavailable before replication support");

    std::filesystem::remove_all(Root, Error);
}

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
            if (!EngineLoop.ReplaceWorld(
                    Snapshot.WorldData,
                    RestoreError,
                    {Pico::EPropertyChangeType::UndoRedo}))
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

    Pico::PActor* DefaultOwner = EngineLoop.GetWorld() != nullptr
        ? EngineLoop.GetWorld()->SpawnActor<Pico::PActor>("DefaultOwner")
        : nullptr;
    Pico::PSceneComponent* DefaultComponent = DefaultOwner != nullptr
        ? Pico::NewObject<Pico::PSceneComponent>(
            DefaultOwner,
            "DefaultComponent",
            Pico::EObjectFlags::DefaultSubobject)
        : nullptr;
    Selection.Set(DefaultComponent);
    Runner.Expect(
        DefaultComponent != nullptr
            && !Commands.RenameObject(
                DefaultComponent->GetHandle(), "RenamedDefaultComponent").bSucceeded
            && !Commands.DeleteSelectedObject().bSucceeded
            && Pico::ResolveObject(DefaultComponent->GetHandle()) == DefaultComponent,
        "Editor commands preserve the identity of default subobjects");
    Selection.Set(EngineLoop.GetWorld());

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

    const Pico::FEditorCommandResult SpawnSpringArm =
        Commands.SpawnComponentActor(Pico::EEditorSceneComponentType::SpringArm);
    Pico::PActor* CameraRig = Selection.Resolve() != nullptr
        && Selection.Resolve()->IsA(Pico::PActor::StaticClass())
        ? static_cast<Pico::PActor*>(Selection.Resolve()) : nullptr;
    Pico::PSpringArmComponent* SpringArm = CameraRig != nullptr
        && CameraRig->GetRootComponent() != nullptr
        && CameraRig->GetRootComponent()->IsA(
            Pico::PSpringArmComponent::StaticClass())
        ? static_cast<Pico::PSpringArmComponent*>(CameraRig->GetRootComponent())
        : nullptr;
    Runner.Expect(
        SpawnSpringArm.bSucceeded && SpringArm != nullptr,
        "Command service transactionally spawns a SpringArm Actor");
    Selection.Set(SpringArm);
    const Pico::FEditorCommandResult AddCamera =
        Commands.AddComponent(Pico::EEditorSceneComponentType::Camera);
    Pico::PCameraComponent* Camera = Selection.Resolve() != nullptr
        && Selection.Resolve()->IsA(Pico::PCameraComponent::StaticClass())
        ? static_cast<Pico::PCameraComponent*>(Selection.Resolve()) : nullptr;
    Runner.Expect(
        AddCamera.bSucceeded
            && Camera != nullptr
            && Camera->GetAttachParent() == SpringArm
            && Camera->GetAttachSocketName()
                == Pico::PSpringArmComponent::GetEndpointSocketName(),
        "Adding a Camera to a SpringArm automatically uses its endpoint socket");
    Pico::FSceneView ActiveCameraView;
    Runner.Expect(
        Pico::TryBuildActiveCameraView(EngineLoop.GetWorld(), ActiveCameraView)
            && Camera != nullptr
            && ActiveCameraView.Position.Equals(Camera->GetViewPosition()),
        "The renderer resolves the active Camera from the runtime scene");

    const Pico::FEditorCommandResult SpawnCameraActor =
        Commands.SpawnActor(Pico::PCameraActor::StaticClass());
    Runner.Expect(
        SpawnCameraActor.bSucceeded
            && Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PCameraActor::StaticClass())
            && static_cast<Pico::PCameraActor*>(Selection.Resolve())
                ->GetCameraComponent() != nullptr,
        "The Actor class workflow creates a ready-to-use CameraActor");

    Runner.Expect(
        Commands.SpawnComponentActor(
            Pico::EEditorSceneComponentType::DirectionalLight).bSucceeded
            && Commands.SpawnComponentActor(
                Pico::EEditorSceneComponentType::PointLight).bSucceeded,
        "Command service transactionally spawns both supported Light types");
    const Pico::FSceneLighting Lighting =
        Pico::GatherSceneLighting(EngineLoop.GetWorld());
    Runner.Expect(
        Lighting.bHasAuthoredLights
            && Lighting.DirectionalLight.bEnabled
            && Lighting.PointLightCount == 1,
        "The renderer gathers authored Directional and Point Lights from the scene");

    const Pico::FEditorCommandResult NoStartValidation =
        Commands.ValidateGameplayForPlay();
    Runner.Expect(
        NoStartValidation.bSucceeded
            && NoStartValidation.Message.find("no PlayerStart") != std::string::npos,
        "Play validation warns when a scene has no authored PlayerStart");

    const Pico::FEditorCommandResult PlayerStartResult = Commands.SpawnPlayerStart();
    Runner.Expect(
        PlayerStartResult.bSucceeded
            && Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PPlayerStart::StaticClass())
            && static_cast<Pico::PPlayerStart*>(Selection.Resolve())
                ->GetRootComponent() != nullptr,
        "Command service transactionally creates and selects a PlayerStart");
    auto* FirstPlayerStart = Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PPlayerStart::StaticClass())
        ? static_cast<Pico::PPlayerStart*>(Selection.Resolve())
        : nullptr;
    if (FirstPlayerStart != nullptr)
    {
        FirstPlayerStart->SetPlayerStartId(7);
    }
    const Pico::FEditorCommandResult ValidGameplay =
        Commands.ValidateGameplayForPlay();
    Runner.Expect(
        ValidGameplay.bSucceeded
            && ValidGameplay.Message.find("passed") != std::string::npos,
        "Play validation accepts a rooted PlayerStart with a unique ID");

    const Pico::FEditorCommandResult SecondStartResult = Commands.SpawnPlayerStart();
    auto* SecondPlayerStart = Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PPlayerStart::StaticClass())
        ? static_cast<Pico::PPlayerStart*>(Selection.Resolve())
        : nullptr;
    if (SecondPlayerStart != nullptr)
    {
        SecondPlayerStart->SetPlayerStartId(7);
    }
    const Pico::FEditorCommandResult DuplicateStartValidation =
        Commands.ValidateGameplayForPlay();
    Runner.Expect(
        SecondStartResult.bSucceeded
            && DuplicateStartValidation.bSucceeded
            && DuplicateStartValidation.Message.find("duplicate") != std::string::npos,
        "Play validation warns about duplicate PlayerStart IDs without blocking Standalone");

    const Pico::FEditorCommandResult PlayableCharacterResult =
        Commands.SpawnPlayableCharacter(Pico::PPawn::StaticClass(), {});
    auto* PlayablePawn = Selection.Resolve() != nullptr
            && Selection.Resolve()->IsA(Pico::PPawn::StaticClass())
        ? static_cast<Pico::PPawn*>(Selection.Resolve()) : nullptr;
    const Pico::FEditorCommandResult AuthoredPawnValidation =
        Commands.ValidateGameplayForPlay();
    Runner.Expect(
        PlayableCharacterResult.bSucceeded
            && PlayablePawn != nullptr
            && PlayablePawn->GetAutoPossessPlayerIndex() == 0
            && AuthoredPawnValidation.bSucceeded
            && AuthoredPawnValidation.Message.find("authored Pawn") != std::string::npos,
        "Playable Character creation authors and validates one Player 0 Pawn");
    Runner.Expect(
        !Commands.SpawnPlayableCharacter(Pico::PPawn::StaticClass(), {}).bSucceeded,
        "Playable Character creation rejects a second Player 0 Pawn");

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
            if (!EngineLoop.ReplaceWorld(
                    Snapshot.WorldData,
                    RestoreError,
                    {Pico::EPropertyChangeType::UndoRedo}))
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

void TestEditorWorldDocument(FTestRunner& Runner)
{
    char Program[] = "PicoEditorDocumentTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, MaxFPS };
    const std::filesystem::path SourceProjectRoot = std::filesystem::absolute(
        "Projects/PicoSandbox").lexically_normal();
    const std::filesystem::path TestProjectRoot =
        std::filesystem::temp_directory_path()
        / "PicoEditorDocumentTests"
        / "PicoSandbox";
    const bool bFixtureReady =
        CopyEditorTestProject(SourceProjectRoot, TestProjectRoot);
    Runner.Expect(
        bFixtureReady,
        "Editor document test prepares an isolated project fixture");
    if (!bFixtureReady)
    {
        return;
    }
    const std::filesystem::path ProjectFile =
        TestProjectRoot / "PicoSandbox.pico";

    Pico::FEngineLoop EngineLoop;
    const bool bInitialized = EngineLoop.PreInit(2, Arguments, ProjectFile) == 0
        && EngineLoop.Init() == 0;
    Runner.Expect(
        bInitialized,
        "Editor document test initializes a project World");
    Runner.Expect(
        bInitialized && PicoSandbox::RegisterSandboxGameplayClasses(),
        "Editor document test registers the active project's gameplay classes");
    Runner.Expect(
        bInitialized
            && Pico::CompileProjectActorBlueprints(EngineLoop.GetAssetRegistry()),
        "Editor document test compiles project Actor Blueprints before opening Worlds");

    Pico::FEditorWorldDocument Document(&EngineLoop);
    Pico::FAssetPath WorldAssetPath;
    Runner.Expect(
        Pico::FAssetPath::TryParse(
            "/Game/Maps/StarterWorld.pworld", WorldAssetPath),
        "World asset path parses");
    const Pico::FEditorDocumentResult OpenResult = Document.Open(WorldAssetPath);
    Runner.Expect(
        OpenResult.bSucceeded
            && Document.HasAssetPath()
            && !Document.IsDirty()
            && Document.GetAssetPath() == WorldAssetPath,
        "Opening a World establishes a clean document identity");

    Pico::FAssetPath RoundTripPath;
    Runner.Expect(
        Pico::FEditorWorldDocument::TryMakeAssetPath(
            Document.GetFilePath(), RoundTripPath)
            && RoundTripPath == WorldAssetPath,
        "World logical and physical paths round trip");

    Pico::PWorld* LoadedWorld = EngineLoop.GetWorld();
    Document.MarkDirty();
    const Pico::FEditorDocumentResult FailedOpen =
        Document.Open(ProjectFile);
    Runner.Expect(
        !FailedOpen.bSucceeded
            && Document.IsDirty()
            && Document.GetAssetPath() == WorldAssetPath
            && EngineLoop.GetWorld() == LoadedWorld,
        "A rejected open preserves the current World and document state");

    const Pico::FEditorDocumentResult NewResult = Document.NewWorld();
    Runner.Expect(
        NewResult.bSucceeded
            && !Document.HasAssetPath()
            && !Document.IsDirty()
            && Document.GetDisplayName() == "Untitled",
        "New World resets the document to an untitled clean state");

    EngineLoop.Exit();
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Editor document tests release the World");

    std::error_code Error;
    std::filesystem::remove_all(TestProjectRoot.parent_path(), Error);
}
}

int main()
{
    FTestRunner Runner;
    TestEditorProjectManager(Runner);
    TestViewportRenderOptionDefaults(Runner);
    TestPlaySessionSettings(Runner);
    TestEditorCommandService(Runner);
    TestEditorTransactions(Runner);
    TestEditorWorldDocument(Runner);
    return Runner.Finish();
}
