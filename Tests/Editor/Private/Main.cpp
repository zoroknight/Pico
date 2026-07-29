#include "Pico/Editor/EditorTransactionManager.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Property.h"
#include "TestRunner.h"

#include <string>
#include <string_view>

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
            RestoredSelectionPath = Snapshot.SelectedObjectPath;
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

    EngineLoop.Exit();
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Editor transaction tests release all reconstructed objects");
}
}

int main()
{
    FTestRunner Runner;
    TestEditorTransactions(Runner);
    return Runner.Finish();
}
