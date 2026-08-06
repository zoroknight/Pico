#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransactionManager.h"

#include <string>
#include <vector>

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
    FEditorCommandResult SpawnStaticMeshActor(const FAssetPath& AssetPath);
    FEditorCommandResult AddSceneRoot();
    FEditorCommandResult AddComponent(bool bCubeComponent);
    FEditorCommandResult AddStaticMeshComponent(const FAssetPath& AssetPath);
    FEditorCommandResult AssignStaticMeshAsset(const FAssetPath& AssetPath);
    FEditorCommandResult ClearStaticMeshAsset();
    FEditorCommandResult AssignMaterialAsset(const FAssetPath& AssetPath);
    FEditorCommandResult ClearMaterialAsset();
    FEditorCommandResult ReplaceAssetReferences(
        const FAssetPath& OldAssetPath,
        const FAssetPath& NewAssetPath,
        EAssetType AssetType);
    FEditorCommandResult ClearStaticMeshAssetReferences(const FAssetPath& AssetPath);
    FEditorCommandResult ClearStaticMeshAssetReferences(
        const std::vector<FAssetPath>& AssetPaths);
    FEditorCommandResult ClearAssetReferences(
        const std::vector<FAssetPath>& AssetPaths);
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
    PActor* CreateStaticMeshActor(std::string Name, const FAssetPath& AssetPath);
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
    FEditorCommandResult SetSelectedStaticMeshAsset(
        const FAssetPath& AssetPath,
        bool bClear);
    FEditorCommandResult SetSelectedMaterialAsset(
        const FAssetPath& AssetPath,
        bool bClear);

    FEngineLoop* EngineLoop = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorTransactionManager* Transactions = nullptr;
    FEditorSceneClipboard* Clipboard = nullptr;
    unsigned int NextActorNumber = 1;
    unsigned int NextCubeNumber = 1;
    unsigned int NextStaticMeshNumber = 1;
    unsigned int NextComponentNumber = 1;
    unsigned int NextCubeComponentNumber = 1;
    unsigned int NextStaticMeshComponentNumber = 1;
};
}
