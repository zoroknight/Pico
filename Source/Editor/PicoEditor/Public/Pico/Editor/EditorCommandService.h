#pragma once

#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransactionManager.h"

#include <string>

namespace Pico
{
class FEngineLoop;
class PActor;
class PSceneComponent;
class PWorld;

struct FEditorCommandResult
{
    bool bSucceeded = false;
    std::string Message;
};

class FEditorCommandService
{
public:
    FEditorCommandService(
        FEngineLoop* EngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        FEditorSceneClipboard* Clipboard);

    FEditorCommandResult SpawnActor(bool bCubeActor);
    FEditorCommandResult AddSceneRoot();
    FEditorCommandResult AddComponent(bool bCubeComponent);
    FEditorCommandResult SetSelectedComponentAsRoot();
    FEditorCommandResult DeleteSelectedObject();
    FEditorCommandResult RenameObject(FObjectHandle ObjectHandle, std::string NewName);
    FEditorCommandResult CopySelectedObject();
    FEditorCommandResult PasteClipboard();
    FEditorCommandResult SaveWorld();
    FEditorCommandResult OpenWorld();
    FEditorCommandResult Undo();
    FEditorCommandResult Redo();

    bool CanCopySelectedObject() const;
    bool CanPasteClipboard() const;

private:
    PWorld* GetWorld() const;
    PActor* CreateActor(std::string Name, bool bCubeActor);
    PSceneComponent* CreateSceneRoot(PActor* Actor);
    PSceneComponent* CreateComponent(
        PActor* Actor,
        PSceneComponent* Parent,
        bool bCubeComponent);
    bool BeginTransaction(std::string Description, EWorldSerializationError& OutError);
    bool CommitTransaction(EWorldSerializationError& OutError);
    bool RollbackTransaction(EWorldSerializationError& OutError);
    bool RestoreSnapshot(
        const FEditorWorldSnapshot& Snapshot,
        EWorldSerializationError* OutError);

    FEngineLoop* EngineLoop = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorTransactionManager* Transactions = nullptr;
    FEditorSceneClipboard* Clipboard = nullptr;
    unsigned int NextActorNumber = 1;
    unsigned int NextCubeNumber = 1;
    unsigned int NextComponentNumber = 1;
    unsigned int NextCubeComponentNumber = 1;
};
}
