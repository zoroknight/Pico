#include "Pico/Editor/EditorCommandService.h"

#include "Pico/Core/Paths.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectName.h"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
FEditorCommandResult Success(std::string Message)
{
    return { true, std::move(Message) };
}

FEditorCommandResult Failure(std::string Message)
{
    return { false, std::move(Message) };
}
}

FEditorCommandService::FEditorCommandService(
    FEngineLoop* InEngineLoop,
    FEditorSelection* InSelection,
    FEditorTransactionManager* InTransactions,
    FEditorSceneClipboard* InClipboard)
    : EngineLoop(InEngineLoop)
    , Selection(InSelection)
    , Transactions(InTransactions)
    , Clipboard(InClipboard)
{
}

PWorld* FEditorCommandService::GetWorld() const
{
    return EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
}

PActor* FEditorCommandService::CreateActor(std::string Name, bool bCubeActor)
{
    PWorld* World = GetWorld();
    PActor* Actor = World != nullptr ? World->SpawnActor<PActor>(Name) : nullptr;
    if (Actor == nullptr)
    {
        return nullptr;
    }
    if (bCubeActor)
    {
        PCubeComponent* Cube = Actor->CreateComponent<PCubeComponent>("CubeComponent");
        if (Cube == nullptr || !Actor->SetRootComponent(Cube))
        {
            World->DestroyActor(Actor);
            return nullptr;
        }
    }
    else if (CreateSceneRoot(Actor) == nullptr)
    {
        World->DestroyActor(Actor);
        return nullptr;
    }
    return Actor;
}

PSceneComponent* FEditorCommandService::CreateSceneRoot(PActor* Actor)
{
    if (Actor == nullptr || Actor->GetRootComponent() != nullptr)
    {
        return nullptr;
    }
    PSceneComponent* Root = Actor->CreateComponent<PSceneComponent>("DefaultSceneRoot");
    if (Root == nullptr || !Actor->SetRootComponent(Root))
    {
        if (Root != nullptr)
        {
            Actor->DestroyComponent(Root);
        }
        return nullptr;
    }
    return Root;
}

PActor* FEditorCommandService::CreateStaticMeshActor(
    std::string Name,
    const FAssetPath& AssetPath)
{
    PWorld* World = GetWorld();
    PActor* Actor = World != nullptr ? World->SpawnActor<PActor>(Name) : nullptr;
    if (Actor == nullptr)
    {
        return nullptr;
    }
    PStaticMeshComponent* Component =
        Actor->CreateComponent<PStaticMeshComponent>("StaticMeshComponent");
    if (Component == nullptr || !Actor->SetRootComponent(Component))
    {
        World->DestroyActor(Actor);
        return nullptr;
    }
    Component->SetStaticMeshAsset(AssetPath);
    return Actor;
}

PSceneComponent* FEditorCommandService::CreateComponent(
    PActor* Actor,
    PSceneComponent* Parent,
    bool bCubeComponent)
{
    if (Actor == nullptr || (Parent != nullptr && Parent->GetOwner() != Actor))
    {
        return nullptr;
    }
    unsigned int& NextNumber = bCubeComponent ? NextCubeComponentNumber : NextComponentNumber;
    const char* Prefix = bCubeComponent ? "CubeComponent_" : "SceneComponent_";
    PSceneComponent* Component = nullptr;
    do
    {
        const std::string Name = Prefix + std::to_string(NextNumber++);
        Component = bCubeComponent
            ? static_cast<PSceneComponent*>(Actor->CreateComponent<PCubeComponent>(Name))
            : Actor->CreateComponent<PSceneComponent>(Name);
    }
    while (Component == nullptr && NextNumber < 10000);

    if (Component == nullptr)
    {
        return nullptr;
    }
    PSceneComponent* Root = Actor->GetRootComponent();
    const bool bConnected = Root == nullptr
        ? Actor->SetRootComponent(Component)
        : Component->AttachToComponent(
            Parent != nullptr ? Parent : Root,
            EAttachmentTransformRule::KeepRelative);
    if (!bConnected)
    {
        Actor->DestroyComponent(Component);
        return nullptr;
    }
    return Component;
}

bool FEditorCommandService::BeginTransaction(
    std::string Description,
    EWorldSerializationError& OutError)
{
    PWorld* World = GetWorld();
    return Transactions != nullptr && Selection != nullptr && World != nullptr
        && Transactions->Begin(
            std::move(Description),
            *World,
            Selection->GetObjectPaths(),
            Selection->GetObjectPath(),
            &OutError);
}

bool FEditorCommandService::CommitTransaction(EWorldSerializationError& OutError)
{
    PWorld* World = GetWorld();
    if (Transactions != nullptr && Selection != nullptr && World != nullptr
        && Transactions->Commit(
            *World,
            Selection->GetObjectPaths(),
            Selection->GetObjectPath(),
            &OutError))
    {
        return true;
    }
    EWorldSerializationError RollbackError = EWorldSerializationError::None;
    RollbackTransaction(RollbackError);
    return false;
}

bool FEditorCommandService::RollbackTransaction(EWorldSerializationError& OutError)
{
    return Transactions != nullptr
        && Transactions->Rollback(
            [this](const FEditorWorldSnapshot& Snapshot, EWorldSerializationError* Error)
            {
                return RestoreSnapshot(Snapshot, Error);
            },
            &OutError);
}

bool FEditorCommandService::RestoreSnapshot(
    const FEditorWorldSnapshot& Snapshot,
    EWorldSerializationError* OutError)
{
    if (EngineLoop == nullptr || Selection == nullptr
        || !EngineLoop->ReplaceWorld(Snapshot.WorldData, OutError))
    {
        return false;
    }
    Selection->Restore(
        GetWorld(),
        Snapshot.SelectedObjectPaths,
        Snapshot.PrimaryObjectPath);
    return true;
}

FEditorCommandResult FEditorCommandService::SpawnActor(bool bCubeActor)
{
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(bCubeActor ? "Create Cube Actor" : "Create Empty Actor", Error))
    {
        return Failure("Could not begin create transaction: " + std::string(ToString(Error)));
    }
    unsigned int& Number = bCubeActor ? NextCubeNumber : NextActorNumber;
    const char* Prefix = bCubeActor ? "Cube_" : "Actor_";
    PActor* Actor = nullptr;
    do
    {
        Actor = CreateActor(Prefix + std::to_string(Number++), bCubeActor);
    }
    while (Actor == nullptr && Number < 10000);
    if (Actor == nullptr)
    {
        RollbackTransaction(Error);
        return Failure(bCubeActor ? "Failed to spawn a Cube" : "Failed to spawn an Actor");
    }
    const std::string Path = Actor->GetPathName();
    Selection->Set(Actor);
    if (!CommitTransaction(Error))
    {
        return Failure("Could not commit create transaction: " + std::string(ToString(Error)));
    }
    return Success("Spawned " + Path);
}

FEditorCommandResult FEditorCommandService::SpawnStaticMeshActor(
    const FAssetPath& AssetPath)
{
    if (!AssetPath.IsValid())
    {
        return Failure("Select a valid Static Mesh asset");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Create Static Mesh Actor", Error))
    {
        return Failure("Could not begin create transaction");
    }
    PActor* Actor = nullptr;
    do
    {
        Actor = CreateStaticMeshActor(
            "StaticMesh_" + std::to_string(NextStaticMeshNumber++),
            AssetPath);
    }
    while (Actor == nullptr && NextStaticMeshNumber < 10000);
    if (Actor == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Failed to spawn a Static Mesh Actor");
    }
    const std::string Path = Actor->GetPathName();
    Selection->Set(Actor);
    return CommitTransaction(Error) ? Success("Spawned " + Path)
                                    : Failure("Could not commit create transaction");
}

FEditorCommandResult FEditorCommandService::AddSceneRoot()
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    if (Actor == nullptr)
    {
        return Failure("Select an Actor without a root component");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Add Scene Root", Error))
    {
        return Failure("Could not begin add-root transaction");
    }
    PSceneComponent* Root = CreateSceneRoot(Actor);
    if (Root == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Selected Actor could not create a scene root");
    }
    const std::string Path = Root->GetPathName();
    Selection->Set(Root);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-root transaction");
}

FEditorCommandResult FEditorCommandService::AddComponent(bool bCubeComponent)
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    PSceneComponent* Parent = Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    if (Actor == nullptr && Parent != nullptr)
    {
        Actor = Parent->GetOwner();
    }
    if (Actor == nullptr)
    {
        return Failure("Select an Actor or SceneComponent before adding a component");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(bCubeComponent ? "Add Cube Component" : "Add Scene Component", Error))
    {
        return Failure("Could not begin add-component transaction");
    }
    PSceneComponent* Component = CreateComponent(Actor, Parent, bCubeComponent);
    if (Component == nullptr)
    {
        RollbackTransaction(Error);
        return Failure("Could not add the selected component type");
    }
    const std::string Path = Component->GetPathName();
    Selection->Set(Component);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-component transaction");
}

FEditorCommandResult FEditorCommandService::AddStaticMeshComponent(
    const FAssetPath& AssetPath)
{
    if (!AssetPath.IsValid())
    {
        return Failure("Select a valid Static Mesh asset");
    }
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
    PSceneComponent* Parent = Object != nullptr
        && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    if (Actor == nullptr && Parent != nullptr)
    {
        Actor = Parent->GetOwner();
    }
    if (Actor == nullptr)
    {
        return Failure("Select an Actor or SceneComponent before adding a component");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Add Static Mesh Component", Error))
    {
        return Failure("Could not begin add-component transaction");
    }
    PStaticMeshComponent* Component = nullptr;
    do
    {
        Component = Actor->CreateComponent<PStaticMeshComponent>(
            "StaticMeshComponent_"
            + std::to_string(NextStaticMeshComponentNumber++));
    }
    while (Component == nullptr && NextStaticMeshComponentNumber < 10000);
    const bool bConnected = Component != nullptr
        && (Actor->GetRootComponent() == nullptr
            ? Actor->SetRootComponent(Component)
            : Component->AttachToComponent(
                Parent != nullptr ? Parent : Actor->GetRootComponent(),
                EAttachmentTransformRule::KeepRelative));
    if (!bConnected)
    {
        if (Component != nullptr)
        {
            Actor->DestroyComponent(Component);
        }
        RollbackTransaction(Error);
        return Failure("Could not add a Static Mesh Component");
    }
    Component->SetStaticMeshAsset(AssetPath);
    const std::string Path = Component->GetPathName();
    Selection->Set(Component);
    return CommitTransaction(Error) ? Success("Added " + Path)
                                    : Failure("Could not commit add-component transaction");
}

FEditorCommandResult FEditorCommandService::SetSelectedComponentAsRoot()
{
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    PSceneComponent* Component = Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object) : nullptr;
    PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
    if (Owner == nullptr)
    {
        return Failure("Selected component could not become the root");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Set Root Component", Error) || !Owner->SetRootComponent(Component))
    {
        if (Transactions != nullptr && Transactions->HasPendingTransaction())
        {
            RollbackTransaction(Error);
        }
        return Failure("Selected component could not become the root");
    }
    const std::string Path = Component->GetPathName();
    return CommitTransaction(Error) ? Success("Set " + Path + " as RootComponent")
                                    : Failure("Could not commit root-component transaction");
}

FEditorCommandResult FEditorCommandService::DeleteSelectedObject()
{
    if (Selection == nullptr)
    {
        return Failure("Only Actors and Components can be destroyed");
    }

    std::vector<FObjectHandle> ComponentHandles;
    std::vector<FObjectHandle> ActorHandles;
    for (PObject* Object : Selection->ResolveAll())
    {
        if (Object->IsA(PActor::StaticClass()))
        {
            ActorHandles.push_back(Object->GetHandle());
            continue;
        }
        if (!Object->IsA(PActorComponent::StaticClass()))
        {
            continue;
        }

        PActorComponent* Component = static_cast<PActorComponent*>(Object);
        if (Selection->Contains(Component->GetOwner()))
        {
            continue;
        }
        bool bAncestorSelected = false;
        if (Component->IsA(PSceneComponent::StaticClass()))
        {
            for (PSceneComponent* Parent =
                     static_cast<PSceneComponent*>(Component)->GetAttachParent();
                 Parent != nullptr;
                 Parent = Parent->GetAttachParent())
            {
                if (Selection->Contains(Parent))
                {
                    bAncestorSelected = true;
                    break;
                }
            }
        }
        if (!bAncestorSelected)
        {
            ComponentHandles.push_back(Object->GetHandle());
        }
    }

    const std::size_t ObjectCount = ComponentHandles.size() + ActorHandles.size();
    if (ObjectCount == 0)
    {
        return Failure("Only Actors and Components can be destroyed");
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    const std::string Description = ObjectCount == 1
        ? "Delete Object" : "Delete " + std::to_string(ObjectCount) + " Objects";
    if (!BeginTransaction(Description, Error))
    {
        return Failure("Could not begin delete transaction");
    }

    for (FObjectHandle Handle : ComponentHandles)
    {
        PObject* Object = ResolveObject(Handle);
        PActorComponent* Component = Object != nullptr
            && Object->IsA(PActorComponent::StaticClass())
            ? static_cast<PActorComponent*>(Object) : nullptr;
        PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
        if (Owner == nullptr || !Owner->DestroyComponent(Component))
        {
            RollbackTransaction(Error);
            return Failure("Could not destroy the selected Components");
        }
    }
    for (FObjectHandle Handle : ActorHandles)
    {
        PObject* Object = ResolveObject(Handle);
        PActor* Actor = Object != nullptr && Object->IsA(PActor::StaticClass())
            ? static_cast<PActor*>(Object) : nullptr;
        PWorld* World = Actor != nullptr ? Actor->GetWorld() : nullptr;
        if (World == nullptr || !World->DestroyActor(Actor))
        {
            RollbackTransaction(Error);
            return Failure("Could not destroy the selected Actors");
        }
    }

    Selection->Set(GetWorld());
    return CommitTransaction(Error)
        ? Success("Destroyed " + std::to_string(ObjectCount) + " object(s)")
                                    : Failure("Could not commit delete transaction");
}

FEditorCommandResult FEditorCommandService::RenameObject(
    FObjectHandle ObjectHandle,
    std::string NewName)
{
    PObject* Object = ResolveObject(ObjectHandle);
    if (Object == nullptr)
    {
        return Failure("The object being renamed no longer exists");
    }
    if (!IsValidObjectName(NewName))
    {
        return Failure("Names must start with a letter or underscore and contain only letters, numbers, or underscores");
    }
    if (Object->GetName() == FName(NewName))
    {
        return Success("Name unchanged");
    }
    const std::string OldPath = Object->GetPathName();
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction("Rename " + Object->GetName().ToString(), Error)
        || !Pico::RenameObject(Object, FName(NewName)))
    {
        if (Transactions != nullptr && Transactions->HasPendingTransaction())
        {
            RollbackTransaction(Error);
        }
        return Failure("The name is already used in this object scope");
    }
    const std::string NewPath = Object->GetPathName();
    return CommitTransaction(Error) ? Success("Renamed " + OldPath + " to " + NewPath)
                                    : Failure("Could not commit rename transaction");
}

bool FEditorCommandService::CanCopySelectedObject() const
{
    if (Clipboard == nullptr || Selection == nullptr || Selection->Num() == 0)
    {
        return false;
    }
    const std::vector<PObject*> Objects = Selection->ResolveAll();
    if (Objects.size() != Selection->Num())
    {
        return false;
    }
    const bool bAllActors = std::all_of(
        Objects.begin(),
        Objects.end(),
        [](PObject* Object) { return Object->IsA(PActor::StaticClass()); });
    return bAllActors
        || (Objects.size() == 1
            && Objects.front()->IsA(PSceneComponent::StaticClass()));
}

bool FEditorCommandService::CanPasteClipboard() const
{
    if (Clipboard == nullptr || !Clipboard->HasContent() || GetWorld() == nullptr)
    {
        return false;
    }
    if (Clipboard->GetContentType() == EEditorClipboardContentType::Actor)
    {
        return true;
    }
    PObject* Object = Selection != nullptr ? Selection->Resolve() : nullptr;
    return Object != nullptr && (Object->IsA(PActor::StaticClass())
        || Object->IsA(PSceneComponent::StaticClass()));
}

FEditorCommandResult FEditorCommandService::CopySelectedObject()
{
    PWorld* World = GetWorld();
    if (World == nullptr || Selection == nullptr || !CanCopySelectedObject())
    {
        return Failure("Select one or more Actors, or one SceneComponent, to copy");
    }
    const std::vector<std::string> Paths = Selection->GetObjectPaths();
    EEditorClipboardError Error = EEditorClipboardError::None;
    return Clipboard->Copy(*World, Paths, &Error)
        ? Success("Copied " + std::to_string(Paths.size()) + " object(s)")
        : Failure("Could not copy object: " + std::string(ToString(Error)));
}

FEditorCommandResult FEditorCommandService::PasteClipboard()
{
    PWorld* World = GetWorld();
    if (!CanPasteClipboard() || EngineLoop == nullptr || Selection == nullptr)
    {
        return Failure("Clipboard cannot be pasted at the current selection");
    }
    FWorldAssetData Data;
    std::vector<std::string> PastedPaths;
    EEditorClipboardError ClipboardError = EEditorClipboardError::None;
    if (!Clipboard->BuildPaste(
            *World,
            Selection->GetObjectPath(),
            Data,
            PastedPaths,
            &ClipboardError))
    {
        return Failure("Could not build pasted object: " + std::string(ToString(ClipboardError)));
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!BeginTransaction(Clipboard->GetContentType() == EEditorClipboardContentType::Actor
            ? "Paste Actor" : "Paste Component", Error)
        || !EngineLoop->ReplaceWorld(Data, &Error))
    {
        if (Transactions != nullptr && Transactions->HasPendingTransaction())
        {
            RollbackTransaction(Error);
        }
        return Failure("Could not paste object: " + std::string(ToString(Error)));
    }
    Selection->Clear();
    for (const std::string& PastedPath : PastedPaths)
    {
        PObject* PastedObject = FindEditorWorldObjectByPath(GetWorld(), PastedPath);
        if (PastedObject == nullptr)
        {
            RollbackTransaction(Error);
            return Failure("Pasted object could not be selected");
        }
        Selection->Add(PastedObject);
    }
    return CommitTransaction(Error)
        ? Success("Pasted " + std::to_string(PastedPaths.size()) + " object(s)")
                                    : Failure("Could not commit paste transaction");
}

FEditorCommandResult FEditorCommandService::SaveWorld()
{
    PWorld* World = GetWorld();
    std::filesystem::path Path;
    if (World == nullptr || !FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Content, std::filesystem::path("Maps") / "EditorWorld.pworld", Path))
    {
        return Failure("Cannot save without an active project World");
    }
    std::error_code FileError;
    std::filesystem::create_directories(Path.parent_path(), FileError);
    if (FileError)
    {
        return Failure("Could not create the project Maps directory");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    return SaveWorldToFile(Path, *World, &Error)
        ? Success("Saved " + Path.string())
        : Failure("Could not save World: " + std::string(ToString(Error)));
}

FEditorCommandResult FEditorCommandService::OpenWorld()
{
    std::filesystem::path Path;
    if (EngineLoop == nullptr || !FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Content, std::filesystem::path("Maps") / "EditorWorld.pworld", Path))
    {
        return Failure("Cannot open a World without an active project");
    }
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!EngineLoop->LoadWorld(Path, &Error))
    {
        return Failure("Could not open World: " + std::string(ToString(Error)));
    }
    Transactions->Clear();
    Selection->Set(GetWorld());
    return Success("Opened " + Path.string());
}

FEditorCommandResult FEditorCommandService::Undo()
{
    if (Transactions == nullptr || !Transactions->CanUndo())
    {
        return Failure("Nothing to undo");
    }
    std::string Description;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!Transactions->Undo(
            [this](const FEditorWorldSnapshot& Snapshot, EWorldSerializationError* RestoreError)
            {
                return RestoreSnapshot(Snapshot, RestoreError);
            },
            &Description,
            &Error))
    {
        return Failure("Undo failed: " + std::string(ToString(Error)));
    }
    return Success("Undid " + Description);
}

FEditorCommandResult FEditorCommandService::Redo()
{
    if (Transactions == nullptr || !Transactions->CanRedo())
    {
        return Failure("Nothing to redo");
    }
    std::string Description;
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!Transactions->Redo(
            [this](const FEditorWorldSnapshot& Snapshot, EWorldSerializationError* RestoreError)
            {
                return RestoreSnapshot(Snapshot, RestoreError);
            },
            &Description,
            &Error))
    {
        return Failure("Redo failed: " + std::string(ToString(Error)));
    }
    return Success("Redid " + Description);
}
}
