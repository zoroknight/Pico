#pragma once

#include "EditorViewportPanel.h"
#include "EditorAssetWorkflowController.h"
#include "ContentBrowserPanel.h"
#include "DetailsPanel.h"
#include "SceneOutlinerPanel.h"
#include "SkeletalAssetEditor.h"
#include "ActorBlueprintEditor.h"
#include "PicoGraphEditor.h"

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/PlatformProcess.h"
#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorAssetSelection.h"
#include "Pico/Editor/EditorAssetService.h"
#include "Pico/Editor/EditorCommandQueue.h"
#include "Pico/Editor/EditorPropertyService.h"
#include "Pico/Editor/PlaySession.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorToolState.h"
#include "Pico/Editor/EditorTransformService.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Editor/EditorWorldDocument.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Tasks/GameThreadDispatcher.h"
#include "Pico/Tasks/TaskSystem.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Pico
{
class FEngineLoop;
class PActor;
struct FPackageOperationState;
class PActorComponent;
class PLevel;
class PObject;
class PProperty;
class PSceneComponent;
class PWorld;
class FSceneViewportRenderer;
class FAgentChatWorkspace;

class FPicoEditorApp
{
public:
    FPicoEditorApp(
        FEngineLoop* EngineLoop,
        FSceneViewportRenderer* ViewportRenderer,
        GLFWwindow* Window);
    ~FPicoEditorApp();

    void Draw();
    void PumpGameThreadTasks();
    void RequestClose();
    bool ShouldClose() const;

private:
    enum class EMessageSeverity
    {
        Info,
        Warning,
        Error
    };

    struct FEditorMessage
    {
        EMessageSeverity Severity = EMessageSeverity::Info;
        std::string Text;
    };

    struct FInputMappingSetting
    {
        std::array<char, 64> Name {};
        std::array<char, 160> Bindings {};
        bool bAxis = false;
    };

    PWorld* GetWorld() const;
    PObject* GetSelectedObject() const;

    void HandleShortcuts();
    void DrawFileMenu();
    void OpenProject(bool bSelectFolder);
    void PerformOpenProject();
    void SaveEditorSession(bool bForce = false);
    void DrawEditMenu();
    void DrawViewMenu();
    void DrawProjectSettings();
    void LoadProjectSettings();
    void SaveProjectSettings();
    void DrawToolbar();
    void DrawViewport(float Width, float Height);
    void DrawStatusBar();
    void DrawMessageLog();
    void PumpCoreLogMessages();
    void DrawPlayValidationPopup();
    void DrawPlaySettingsPopup();
    void DrawUnsavedChangesPopup();
    void DrawRenamePopup();
    void ProcessDeferredActions();
    void StartGame();
    void LaunchGame(const std::string& ValidationMessage);
    void StopGame(bool bUpdateStatus = true);
    void UpdatePlaySession();
    void LoadPlaySettings();
    void SavePlaySettings();
    void DrawPackageProjectPopup();
    void StartPackageProject();
    void UpdatePackageProcess();

    void SpawnEmptyActor();
    void DrawActorClassPicker();
    void DrawPlayableCharacterCreator();
    void SpawnCubeActor();
    void SpawnPlayerStart();
    void SpawnComponentActor(EEditorSceneComponentType Type);
    void SpawnStaticMeshActor();
    void AddRootToSelectedActor();
    void AddComponentToSelection(bool bCubeComponent);
    void AddComponentToSelection(EEditorSceneComponentType Type);
    void AddSceneComponentToSelection();
    void AddCubeComponentToSelection();
    void AddStaticMeshComponentToSelection();
    const FAssetPath* GetSelectedStaticMeshAsset() const;
    const FAssetPath* GetSelectedMaterialAsset() const;
    void CreateStaticMeshActor(const FAssetPath& AssetPath);
    void AssignStaticMeshAsset(const FAssetPath& AssetPath);
    void AssignMaterialAsset(const FAssetPath& AssetPath);
    void AssignSelectedAsset(const FAssetPath& AssetPath);
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
    bool SaveWorld();
    bool SaveWorldAs();
    void OpenWorld();
    void NewWorld();
    void PerformOpenWorld();
    void PerformNewWorld();
    void RequestDocumentAction(int Action);
    void ContinueDocumentAction(int Action);
    void FinishDocumentChange();
    void UpdateWindowTitle();
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
    void SetStatus(
        std::string Message,
        bool bIsError = false,
        bool bIsWarning = false);

    FEditorSelection Selection;
    FEditorToolState ToolState;
    FEditorTransformService TransformService;
    FEditorSceneClipboard SceneClipboard;
    FEditorTransactionManager TransactionManager;
    FEngineLoop* EngineLoop = nullptr;
    GLFWwindow* Window = nullptr;
    FEditorWorldDocument WorldDocument;
    FEditorAssetSelection AssetSelection;
    FEditorAssetService AssetService;
    FEditorCommandService CommandService;
    FEditorPropertyService PropertyService;
    FEditorCommandQueue CommandQueue;
    FEditorViewportPanel ViewportPanel;
    FSceneOutlinerPanel OutlinerPanel;
    FDetailsPanel DetailsPanel;
    FContentBrowserPanel ContentBrowserPanel;
    FSkeletalAssetEditor SkeletalAssetEditor;
    FActorBlueprintEditor ActorBlueprintEditor;
    FPicoGraphEditor PicoGraphEditor;
    FEditorAssetWorkflowController AssetWorkflow;
    FPlaySession PlaySession;
    FPlaySessionSettings PlaySettings;
    FTaskSystem TaskSystem;
    FGameThreadDispatcher GameThreadDispatcher;
    std::unique_ptr<FAgentChatWorkspace> AgentChatWorkspace;
    std::shared_ptr<FPackageOperationState> PackageOperationState;
    FProcessHandle PackageProcess;
    FProcessGroup PackageProcessGroup;
    std::filesystem::path PendingProjectFile;
    std::filesystem::path PlaySettingsFile;
    std::filesystem::path PackageProcessLogFile;
    std::filesystem::path PackageOutputDirectory;
    FAssetPath PendingWorldAssetPath;
    FObjectHandle RenameObjectHandle;
    std::array<char, 128> RenameBuffer {};
    std::array<char, 128> ActorClassFilter {};
    std::array<char, 128> PlayablePawnClassSetting {};
    std::array<char, 260> PlayableCharacterProfileSetting {};
    std::array<char, 260> DefaultMapSetting {};
    std::array<char, 128> DefaultPawnClassSetting {};
    std::array<char, 128> PlayerControllerClassSetting {};
    std::array<char, 260> DefaultPawnProfileSetting {};
    std::array<char, 520> PackageOutputRootSetting {};
    std::array<char, 260> PackageNameSetting {};
    std::vector<FInputMappingSetting> InputMappingSettings;
    float MouseSensitivitySetting = 0.12f;
    std::string InteractiveEditKey;
    std::string Status;
    std::string PendingPlayValidation;
    std::vector<FEditorMessage> Messages;
    std::uint64_t LastObservedLogSequence = 0;
    bool bInteractiveEditChanged = false;
    bool bInteractiveEditVisited = false;
    bool bFinishInteractiveEditRequested = false;
    bool bCancelInteractiveEditRequested = false;
    bool bStatusIsError = false;
    bool bStatusIsWarning = false;
    bool bMessageLogOpen = true;
    bool bAgentChatOpen = true;
    bool bShowFrameRate = true;
    bool bFocusMessageLog = false;
    bool bOpenPlayValidationPopup = false;
    bool bOpenPlaySettingsPopup = false;
    bool bPendingPlayBlocked = false;
    bool bPendingPlayWarning = false;
    bool bPendingPlayNeedsSave = false;
    bool bResetDockLayout = false;
    bool bOpenRenamePopup = false;
    bool bOpenActorClassPicker = false;
    bool bOpenPlayableCharacterCreator = false;
    bool bOpenPackageProjectPopup = false;
    bool bPackageSmokeTest = true;
    bool bProjectSettingsOpen = false;
    bool bProjectSettingsLoaded = false;
    bool bPreviewSceneCamera = false;
    bool bOpenUnsavedChangesPopup = false;
    bool bShouldClose = false;
    int PendingDocumentAction = 0;
    std::string WindowTitle;
    std::string SavedSessionFingerprint;
};
}
