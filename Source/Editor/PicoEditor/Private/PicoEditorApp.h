#pragma once

#include "EditorViewportPanel.h"
#include "DetailsPanel.h"
#include "SceneOutlinerPanel.h"

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorCommandQueue.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorToolState.h"
#include "Pico/Editor/EditorTransformService.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Object/ObjectTypes.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Pico
{
class FEngineLoop;
class PActor;
class PActorComponent;
class PLevel;
class PObject;
class PProperty;
class PSceneComponent;
class PWorld;
class FSceneViewportRenderer;

class FPicoEditorApp
{
public:
    FPicoEditorApp(
        FEngineLoop* EngineLoop,
        FSceneViewportRenderer* ViewportRenderer,
        GLFWwindow* Window);
    ~FPicoEditorApp();

    void Draw();

private:
    PWorld* GetWorld() const;
    PObject* GetSelectedObject() const;

    void HandleShortcuts();
    void DrawFileMenu();
    void DrawEditMenu();
    void DrawToolbar();
    void DrawViewport(float Width, float Height);
    void DrawStatusBar();
    void DrawRenamePopup();
    void ProcessDeferredActions();

    void SpawnEmptyActor();
    void SpawnCubeActor();
    void SpawnStaticMeshActor();
    void AddRootToSelectedActor();
    void AddComponentToSelection(bool bCubeComponent);
    void AddSceneComponentToSelection();
    void AddCubeComponentToSelection();
    void AddStaticMeshComponentToSelection();
    const FAssetPath* FindFirstStaticMeshAsset() const;
    void SetSelectedComponentAsRoot();
    void DestroySelectedObject();
    void QueueDestroy(PObject* Object);
    void BeginRename(PObject* Object);
    void RenameSelectedObject();
    bool CommitRename();
    bool CanCopySelectedObject() const;
    bool CanPasteClipboard() const;
    void CopySelectedObject();
    void PasteClipboard();
    void SaveWorld();
    void OpenWorld();
    bool PrepareInteractiveEdit(
        const std::string& EditKey,
        std::string Description,
        bool bActivated,
        bool bChanged);
    void CompleteInteractiveEdit(
        const std::string& EditKey,
        bool bChanged,
        bool bActive,
        bool bChangeApplied);
    void FinishInteractiveEdit();
    void CancelInteractiveEdit();
    bool BeginEditorTransaction(std::string Description);
    bool CommitEditorTransaction();
    void CancelEditorTransaction();
    void Undo();
    void Redo();
    bool RestoreEditorSnapshot(
        const FEditorWorldSnapshot& Snapshot,
        EWorldSerializationError* OutError);
    std::string GetSelectedObjectPath() const;
    std::vector<std::string> GetSelectedObjectPaths() const;
    void Select(
        PObject* Object,
        EEditorSelectionOperation Operation = EEditorSelectionOperation::Replace,
        const std::vector<PObject*>& OrderedObjects = {});
    void SelectAllActors();
    void ApplyCommandResult(FEditorCommandResult Result);
    void SetStatus(std::string Message, bool bIsError = false);

    FEditorSelection Selection;
    FEditorToolState ToolState;
    FEditorTransformService TransformService;
    FEditorSceneClipboard SceneClipboard;
    FEditorTransactionManager TransactionManager;
    FEngineLoop* EngineLoop = nullptr;
    FEditorCommandService CommandService;
    FEditorCommandQueue CommandQueue;
    FEditorViewportPanel ViewportPanel;
    FSceneOutlinerPanel OutlinerPanel;
    FDetailsPanel DetailsPanel;
    FObjectHandle RenameObjectHandle;
    std::array<char, 128> RenameBuffer {};
    std::string InteractiveEditKey;
    std::string Status;
    bool bInteractiveEditChanged = false;
    bool bInteractiveEditVisited = false;
    bool bStatusIsError = false;
    bool bResetDockLayout = false;
    bool bOpenRenamePopup = false;
};
}
