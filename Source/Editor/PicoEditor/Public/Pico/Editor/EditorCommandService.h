#pragma once

#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransactionManager.h"

#include <functional>
#include <string>
#include <vector>

namespace Pico
{
class FEngineLoop;
class PActor;
class PSceneComponent;
class PWorld;

enum class EEditorSceneComponentType
{
    Camera,
    SpringArm,
    DirectionalLight,
    PointLight,
    SkeletalMesh
};

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
        FEditorSceneClipboard* Clipboard,
        std::function<void()> OnWorldChanged = {});

    FEditorCommandResult SpawnActor(bool bCubeActor);
    FEditorCommandResult SpawnPlayerStart();
    FEditorCommandResult ValidateGameplayForPlay() const;
    FEditorCommandResult SpawnComponentActor(EEditorSceneComponentType Type);
    FEditorCommandResult SpawnStaticMeshActor(const FAssetPath& AssetPath);
    FEditorCommandResult AddSceneRoot();
    FEditorCommandResult AddComponent(bool bCubeComponent);
    FEditorCommandResult AddComponent(EEditorSceneComponentType Type);
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
    PSceneComponent* CreateComponent(
        PActor* Actor,
        PSceneComponent* Parent,
        EEditorSceneComponentType Type);
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
    std::function<void()> OnWorldChanged;
    unsigned int NextActorNumber = 1;
    unsigned int NextCubeNumber = 1;
    unsigned int NextPlayerStartNumber = 1;
    unsigned int NextStaticMeshNumber = 1;
    unsigned int NextComponentNumber = 1;
    unsigned int NextCubeComponentNumber = 1;
    unsigned int NextStaticMeshComponentNumber = 1;
    unsigned int NextCameraNumber = 1;
    unsigned int NextSpringArmNumber = 1;
    unsigned int NextDirectionalLightNumber = 1;
    unsigned int NextPointLightNumber = 1;
    unsigned int NextSkeletalMeshNumber = 1;
    unsigned int NextCameraComponentNumber = 1;
    unsigned int NextSpringArmComponentNumber = 1;
    unsigned int NextDirectionalLightComponentNumber = 1;
    unsigned int NextPointLightComponentNumber = 1;
    unsigned int NextSkeletalMeshComponentNumber = 1;
};
}
