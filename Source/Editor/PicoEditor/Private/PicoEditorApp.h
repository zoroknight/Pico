#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Object/ObjectTypes.h"

#include <array>
#include <filesystem>
#include <string>

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
    void DrawSceneOutliner();
    void DrawViewport(float Width, float Height);
    void DrawLevelNode(PLevel* Level);
    void DrawActorNode(PActor* Actor);
    void DrawComponentNode(PActorComponent* Component, PActor* Owner);
    void DrawActorContextMenu(PActor* Actor);
    void DrawComponentContextMenu(PActorComponent* Component);
    void DrawDetails();
    void DrawObjectIdentity(PObject* Object);
    void DrawActorDetails(PActor* Actor);
    void DrawSceneComponentDetails(PSceneComponent* Component);
    void DrawReflectedProperties(PObject* Object);
    void DrawPropertyEditor(PObject* Object, const PProperty* Property);
    void DrawStatusBar();
    void DrawRenamePopup();
    void ProcessDeferredActions();

    PActor* CreateActor(std::string Name);
    PActor* CreateCubeActor(std::string Name);
    PSceneComponent* AddSceneRoot(PActor* Actor);
    PSceneComponent* AddComponent(
        PActor* Actor,
        PSceneComponent* AttachParent,
        bool bCubeComponent);
    void SpawnEmptyActor();
    void SpawnCubeActor();
    void AddRootToSelectedActor();
    void AddComponentToSelection(bool bCubeComponent);
    void AddSceneComponentToSelection();
    void AddCubeComponentToSelection();
    void SetSelectedComponentAsRoot();
    void DestroySelectedObject();
    void QueueDestroy(PObject* Object);
    void BeginRename(PObject* Object);
    void RenameSelectedObject();
    bool CommitRename();
    void SaveWorld();
    void OpenWorld();
    bool GetDefaultWorldPath(std::filesystem::path& OutPath) const;
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
    void Select(PObject* Object);
    void SetStatus(std::string Message, bool bIsError = false);
    void BeginViewportCameraCapture();
    void EndViewportCameraCapture();

    FObjectHandle SelectedObjectHandle;
    FEditorTransactionManager TransactionManager;
    FEngineLoop* EngineLoop = nullptr;
    FSceneViewportRenderer* ViewportRenderer = nullptr;
    GLFWwindow* Window = nullptr;
    FVector3 CameraPosition = FVector3(530.0f, -530.0f, 400.0f);
    float CameraYawDegrees = 135.0f;
    float CameraPitchDegrees = -28.0f;
    float CameraMoveSpeed = 600.0f;
    unsigned int NextActorNumber = 1;
    unsigned int NextCubeNumber = 1;
    unsigned int NextComponentNumber = 1;
    unsigned int NextCubeComponentNumber = 1;
    FObjectHandle PendingDestroyHandle;
    FObjectHandle RenameObjectHandle;
    std::array<char, 128> RenameBuffer {};
    std::string InteractiveEditKey;
    std::string Status;
    bool bInteractiveEditChanged = false;
    bool bInteractiveEditVisited = false;
    bool bStatusIsError = false;
    bool bResetDockLayout = false;
    bool bOpenRenamePopup = false;
    bool bViewportCameraCaptured = false;
    double LastCameraCursorX = 0.0;
    double LastCameraCursorY = 0.0;
};
}
