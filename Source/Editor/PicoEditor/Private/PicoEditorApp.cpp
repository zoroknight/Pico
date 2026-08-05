#include "PicoEditorApp.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Render/SceneViewportRenderer.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectName.h"
#include "Pico/Object/Property.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
void BuildDefaultDockLayout(ImGuiID DockspaceId, const ImVec2& DockspaceSize)
{
    ImGui::DockBuilderRemoveNode(DockspaceId);
    ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockspaceId, DockspaceSize);

    ImGuiID CenterNodeId = DockspaceId;
    ImGuiID OutlinerNodeId = 0;
    ImGuiID DetailsNodeId = 0;
    ImGui::DockBuilderSplitNode(
        CenterNodeId,
        ImGuiDir_Left,
        0.20f,
        &OutlinerNodeId,
        &CenterNodeId);
    ImGui::DockBuilderSplitNode(
        CenterNodeId,
        ImGuiDir_Right,
        0.28f,
        &DetailsNodeId,
        &CenterNodeId);

    ImGui::DockBuilderDockWindow("Scene Outliner", OutlinerNodeId);
    ImGui::DockBuilderDockWindow("Viewport", CenterNodeId);
    ImGui::DockBuilderDockWindow("Details", DetailsNodeId);
    ImGui::DockBuilderFinish(DockspaceId);
}

}

FPicoEditorApp::FPicoEditorApp(
    FEngineLoop* InEngineLoop,
    FSceneViewportRenderer* InViewportRenderer,
    GLFWwindow* InWindow)
    : EngineLoop(InEngineLoop)
    , CommandService(InEngineLoop, &Selection, &TransactionManager, &SceneClipboard)
    , ViewportPanel(InViewportRenderer, InWindow)
{
    PWorld* World = GetWorld();
    Select(World);
    SetStatus(
        World != nullptr ? "Editor world is ready" : "No active editor World",
        World == nullptr);
}

FPicoEditorApp::~FPicoEditorApp()
{
}

void FPicoEditorApp::Draw()
{
    bInteractiveEditVisited = false;
    HandleShortcuts();

    if (Selection.IsValid() && GetSelectedObject() == nullptr)
    {
        CancelInteractiveEdit();
        Selection.Clear();
    }

    const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(MainViewport->WorkPos);
    ImGui::SetNextWindowSize(MainViewport->WorkSize);
    ImGui::SetNextWindowViewport(MainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(
        "PicoEditorDockspaceHost",
        nullptr,
        ImGuiWindowFlags_MenuBar
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar())
    {
        DrawFileMenu();
        ImGui::SameLine();
        DrawEditMenu();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        DrawToolbar();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        DrawStatusBar();
        ImGui::EndMenuBar();
    }

    const ImGuiID DockspaceId = ImGui::GetID("PicoEditorDockspace");
    const ImVec2 DockspaceSize = ImGui::GetContentRegionAvail();
    const bool bNeedsDefaultLayout =
        bResetDockLayout || ImGui::DockBuilderGetNode(DockspaceId) == nullptr;
    ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f));
    if (bNeedsDefaultLayout)
    {
        BuildDefaultDockLayout(DockspaceId, DockspaceSize);
        bResetDockLayout = false;
    }
    DrawRenamePopup();
    ImGui::End();

    if (ImGui::Begin("Scene Outliner"))
    {
        OutlinerPanel.Draw(
            GetWorld(),
            Selection,
            CommandService,
            CommandQueue,
            [this](PObject* Object) { Select(Object); },
            [this](PObject* Object) { BeginRename(Object); },
            [this](FEditorCommandResult Result) { ApplyCommandResult(std::move(Result)); });
    }
    ImGui::End();

    if (ImGui::Begin(
            "Viewport",
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        DrawViewport(Available.x, Available.y);
    }
    ImGui::End();

    if (ImGui::Begin("Details"))
    {
        DetailsPanel.Draw(
            Selection,
            [this](
                const std::string& Key,
                std::string Description,
                bool bActivated,
                bool bChanged)
            {
                return PrepareInteractiveEdit(
                    Key,
                    std::move(Description),
                    bActivated,
                    bChanged);
            },
            [this](
                const std::string& Key,
                bool bChanged,
                bool bActive,
                bool bApplied)
            {
                CompleteInteractiveEdit(Key, bChanged, bActive, bApplied);
            },
            [this](std::string Message, bool bError)
            {
                SetStatus(std::move(Message), bError);
            },
            [this]()
            {
                AddRootToSelectedActor();
            });
    }
    ImGui::End();

    if (!InteractiveEditKey.empty() && !bInteractiveEditVisited)
    {
        FinishInteractiveEdit();
    }

    ProcessDeferredActions();
}

void FPicoEditorApp::DrawViewport(float Width, float Height)
{
    ViewportPanel.Draw(
        GetWorld(),
        Selection,
        Width,
        Height,
        [this](PObject* Object) { Select(Object); },
        [this](std::string Message) { SetStatus(std::move(Message)); });
}
PWorld* FPicoEditorApp::GetWorld() const
{
    return EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
}

PObject* FPicoEditorApp::GetSelectedObject() const
{
    return Selection.Resolve();
}

void FPicoEditorApp::DrawToolbar()
{
    PObject* SelectedObject = GetSelectedObject();
    PActor* SelectedActor =
        SelectedObject != nullptr && SelectedObject->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(SelectedObject)
        : nullptr;
    PSceneComponent* SelectedSceneComponent =
        SelectedObject != nullptr && SelectedObject->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(SelectedObject)
        : nullptr;

    if (ImGui::BeginMenu("Add"))
    {
        if (ImGui::MenuItem("Empty Actor"))
        {
            SpawnEmptyActor();
        }
        if (ImGui::MenuItem("Cube"))
        {
            SpawnCubeActor();
        }
        ImGui::EndMenu();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(SelectedActor == nullptr || SelectedActor->GetRootComponent() != nullptr);
    if (ImGui::Button("Add Scene Root"))
    {
        AddRootToSelectedActor();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bCanAddComponent =
        SelectedActor != nullptr || SelectedSceneComponent != nullptr;
    ImGui::BeginDisabled(!bCanAddComponent);
    if (ImGui::BeginMenu("Add Component"))
    {
        if (ImGui::MenuItem("Scene Component"))
        {
            AddSceneComponentToSelection();
        }
        if (ImGui::MenuItem("Cube Component"))
        {
            AddCubeComponentToSelection();
        }
        ImGui::EndMenu();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bCanSetRoot =
        SelectedSceneComponent != nullptr
        && SelectedSceneComponent->GetOwner() != nullptr
        && SelectedSceneComponent->GetOwner()->GetRootComponent() != SelectedSceneComponent;
    ImGui::BeginDisabled(!bCanSetRoot);
    if (ImGui::Button("Set As Root"))
    {
        SetSelectedComponentAsRoot();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bCanDestroy =
        SelectedObject != nullptr
        && (SelectedObject->IsA(PActor::StaticClass())
            || SelectedObject->IsA(PActorComponent::StaticClass()));
    ImGui::BeginDisabled(!bCanDestroy);
    if (ImGui::Button("Destroy"))
    {
        DestroySelectedObject();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Reset Layout"))
    {
        bResetDockLayout = true;
    }
}

void FPicoEditorApp::HandleShortcuts()
{
    const ImGuiIO& IO = ImGui::GetIO();
    if (IO.WantTextInput)
    {
        return;
    }

    if (IO.KeyCtrl
        && IO.KeyShift
        && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        Redo();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        Undo();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
    {
        Redo();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
    {
        CopySelectedObject();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
    {
        PasteClipboard();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveWorld();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        OpenWorld();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        DestroySelectedObject();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        RenameSelectedObject();
    }
}

void FPicoEditorApp::DrawFileMenu()
{
    if (!ImGui::BeginMenu("File"))
    {
        return;
    }

    if (ImGui::MenuItem("Open World", "Ctrl+O"))
    {
        OpenWorld();
    }
    if (ImGui::MenuItem("Save World", "Ctrl+S"))
    {
        SaveWorld();
    }
    ImGui::EndMenu();
}

void FPicoEditorApp::DrawEditMenu()
{
    if (!ImGui::BeginMenu("Edit"))
    {
        return;
    }

    std::string UndoLabel = "Undo";
    if (TransactionManager.CanUndo())
    {
        UndoLabel += " " + std::string(TransactionManager.GetUndoDescription());
    }
    if (ImGui::MenuItem(
            UndoLabel.c_str(),
            "Ctrl+Z",
            false,
            TransactionManager.CanUndo()))
    {
        Undo();
    }

    std::string RedoLabel = "Redo";
    if (TransactionManager.CanRedo())
    {
        RedoLabel += " " + std::string(TransactionManager.GetRedoDescription());
    }
    if (ImGui::MenuItem(
            RedoLabel.c_str(),
            "Ctrl+Y",
            false,
            TransactionManager.CanRedo()))
    {
        Redo();
    }

    ImGui::Separator();
    if (ImGui::MenuItem(
            "Copy",
            "Ctrl+C",
            false,
            CanCopySelectedObject()))
    {
        CopySelectedObject();
    }
    if (ImGui::MenuItem(
            "Paste",
            "Ctrl+V",
            false,
            CanPasteClipboard()))
    {
        PasteClipboard();
    }
    ImGui::EndMenu();
}

void FPicoEditorApp::DrawStatusBar()
{
    if (Status.empty())
    {
        ImGui::TextDisabled("Ready");
        return;
    }

    const ImVec4 Color = bStatusIsError
        ? ImVec4(0.95f, 0.42f, 0.35f, 1.0f)
        : ImVec4(0.35f, 0.78f, 0.66f, 1.0f);
    ImGui::TextColored(Color, "%s", Status.c_str());
}

void FPicoEditorApp::DrawRenamePopup()
{
    if (bOpenRenamePopup)
    {
        ImGui::OpenPopup("Rename Object");
        bOpenRenamePopup = false;
    }

    if (!ImGui::BeginPopupModal(
            "Rename Object",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    PObject* Object = ResolveObject(RenameObjectHandle);
    if (Object == nullptr)
    {
        RenameObjectHandle = {};
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Rename %s", Object->GetPathName().c_str());
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool bSubmitted = ImGui::InputText(
        "##ObjectName",
        RenameBuffer.data(),
        RenameBuffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);

    if (bSubmitted || ImGui::Button("Rename"))
    {
        if (CommitRename())
        {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        RenameObjectHandle = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FPicoEditorApp::ProcessDeferredActions()
{
    CommandQueue.Flush();
}

void FPicoEditorApp::SpawnEmptyActor()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SpawnActor(false));
}

void FPicoEditorApp::SpawnCubeActor()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SpawnActor(true));
}

void FPicoEditorApp::AddRootToSelectedActor()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.AddSceneRoot());
}

void FPicoEditorApp::AddComponentToSelection(bool bCubeComponent)
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.AddComponent(bCubeComponent));
}

void FPicoEditorApp::AddSceneComponentToSelection()
{
    AddComponentToSelection(false);
}

void FPicoEditorApp::AddCubeComponentToSelection()
{
    AddComponentToSelection(true);
}

void FPicoEditorApp::SetSelectedComponentAsRoot()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SetSelectedComponentAsRoot());
}

void FPicoEditorApp::DestroySelectedObject()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.DeleteSelectedObject());
}

void FPicoEditorApp::QueueDestroy(PObject* Object)
{
    const FObjectHandle Handle =
        Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    CommandQueue.Enqueue(
        [this, Handle]()
        {
            if (PObject* LiveObject = ResolveObject(Handle))
            {
                Select(LiveObject);
                DestroySelectedObject();
            }
        });
}

void FPicoEditorApp::BeginRename(PObject* Object)
{
    if (Object == nullptr
        || (!Object->IsA(PActor::StaticClass())
            && !Object->IsA(PActorComponent::StaticClass())))
    {
        SetStatus("Only Actors and Components can be renamed", true);
        return;
    }

    RenameObjectHandle = Object->GetHandle();
    RenameBuffer.fill('\0');
    const std::string Name = Object->GetName().ToString();
    const std::size_t CopyLength =
        std::min(Name.size(), RenameBuffer.size() - 1);
    std::copy_n(Name.data(), CopyLength, RenameBuffer.data());
    bOpenRenamePopup = true;
}

void FPicoEditorApp::RenameSelectedObject()
{
    BeginRename(GetSelectedObject());
}

bool FPicoEditorApp::CommitRename()
{
    FinishInteractiveEdit();
    FEditorCommandResult Result = CommandService.RenameObject(
        RenameObjectHandle,
        std::string(RenameBuffer.data()));
    ApplyCommandResult(Result);
    if (!Result.bSucceeded)
    {
        return false;
    }
    RenameObjectHandle = {};
    return true;
}

bool FPicoEditorApp::CanCopySelectedObject() const
{
    return CommandService.CanCopySelectedObject();
}

bool FPicoEditorApp::CanPasteClipboard() const
{
    return CommandService.CanPasteClipboard();
}

void FPicoEditorApp::CopySelectedObject()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.CopySelectedObject());
}

void FPicoEditorApp::PasteClipboard()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.PasteClipboard());
}

void FPicoEditorApp::SaveWorld()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SaveWorld());
}

void FPicoEditorApp::OpenWorld()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.OpenWorld());
}

bool FPicoEditorApp::PrepareInteractiveEdit(
    const std::string& EditKey,
    std::string Description,
    bool bActivated,
    bool bChanged)
{
    if (InteractiveEditKey == EditKey)
    {
        bInteractiveEditVisited = true;
        return true;
    }
    if (!bActivated && !bChanged)
    {
        return false;
    }
    if (!InteractiveEditKey.empty())
    {
        FinishInteractiveEdit();
    }
    if (!BeginEditorTransaction(std::move(Description)))
    {
        return false;
    }

    InteractiveEditKey = EditKey;
    bInteractiveEditChanged = false;
    bInteractiveEditVisited = true;
    return true;
}

void FPicoEditorApp::CompleteInteractiveEdit(
    const std::string& EditKey,
    bool bChanged,
    bool bActive,
    bool bChangeApplied)
{
    if (InteractiveEditKey != EditKey)
    {
        return;
    }

    bInteractiveEditVisited = true;
    if (bChanged)
    {
        if (!bChangeApplied)
        {
            CancelInteractiveEdit();
            SetStatus("Could not apply editor property change", true);
            return;
        }
        bInteractiveEditChanged = true;
    }

    if (!bActive)
    {
        FinishInteractiveEdit();
    }
}

void FPicoEditorApp::FinishInteractiveEdit()
{
    if (InteractiveEditKey.empty())
    {
        return;
    }

    const bool bShouldCommit = bInteractiveEditChanged;
    InteractiveEditKey.clear();
    bInteractiveEditChanged = false;
    bInteractiveEditVisited = false;

    if (bShouldCommit)
    {
        CommitEditorTransaction();
    }
    else
    {
        CancelEditorTransaction();
    }

}

void FPicoEditorApp::CancelInteractiveEdit()
{
    if (InteractiveEditKey.empty())
    {
        return;
    }

    InteractiveEditKey.clear();
    bInteractiveEditChanged = false;
    bInteractiveEditVisited = false;
    CancelEditorTransaction();
}

bool FPicoEditorApp::BeginEditorTransaction(std::string Description)
{
    if (!InteractiveEditKey.empty())
    {
        FinishInteractiveEdit();
    }

    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        SetStatus("Cannot begin a transaction without an active World", true);
        return false;
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!TransactionManager.Begin(
            std::move(Description),
            *World,
            GetSelectedObjectPath(),
            &Error))
    {
        SetStatus(
            "Could not begin editor transaction: "
                + std::string(ToString(Error)),
            true);
        return false;
    }
    return true;
}

bool FPicoEditorApp::CommitEditorTransaction()
{
    PWorld* World = GetWorld();
    EWorldSerializationError Error = EWorldSerializationError::None;
    if (World == nullptr
        || !TransactionManager.Commit(
            *World,
            GetSelectedObjectPath(),
            &Error))
    {
        EWorldSerializationError RollbackError = EWorldSerializationError::None;
        const bool bRolledBack = TransactionManager.Rollback(
            [this](
                const FEditorWorldSnapshot& Snapshot,
                EWorldSerializationError* RestoreError)
            {
                return RestoreEditorSnapshot(Snapshot, RestoreError);
            },
            &RollbackError);
        SetStatus(
            "Could not commit editor transaction: "
                + std::string(ToString(Error))
                + (bRolledBack
                    ? "; changes were rolled back"
                    : "; rollback failed: " + std::string(ToString(RollbackError))),
            true);
        return false;
    }
    return true;
}

void FPicoEditorApp::CancelEditorTransaction()
{
    if (!TransactionManager.HasPendingTransaction())
    {
        return;
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!TransactionManager.Rollback(
            [this](
                const FEditorWorldSnapshot& Snapshot,
                EWorldSerializationError* RestoreError)
            {
                return RestoreEditorSnapshot(Snapshot, RestoreError);
            },
            &Error))
    {
        TransactionManager.Cancel();
        SetStatus(
            "Could not roll back editor transaction: "
                + std::string(ToString(Error)),
            true);
    }
}

void FPicoEditorApp::Undo()
{
    FinishInteractiveEdit();
    RenameObjectHandle = {};
    bOpenRenamePopup = false;
    ApplyCommandResult(CommandService.Undo());
}

void FPicoEditorApp::Redo()
{
    FinishInteractiveEdit();
    RenameObjectHandle = {};
    bOpenRenamePopup = false;
    ApplyCommandResult(CommandService.Redo());
}

bool FPicoEditorApp::RestoreEditorSnapshot(
    const FEditorWorldSnapshot& Snapshot,
    EWorldSerializationError* OutError)
{
    if (EngineLoop == nullptr
        || !EngineLoop->ReplaceWorld(Snapshot.WorldData, OutError))
    {
        return false;
    }

    CommandQueue.Clear();
    RenameObjectHandle = {};
    bOpenRenamePopup = false;
    PWorld* World = GetWorld();
    PObject* RestoredSelection = FindEditorWorldObjectByPath(
        World,
        Snapshot.SelectedObjectPath);
    Select(
        RestoredSelection != nullptr || Snapshot.SelectedObjectPath.empty()
            ? RestoredSelection
            : static_cast<PObject*>(World));
    return true;
}

std::string FPicoEditorApp::GetSelectedObjectPath() const
{
    return Selection.GetObjectPath();
}

void FPicoEditorApp::Select(PObject* Object)
{
    if (Object != GetSelectedObject())
    {
        FinishInteractiveEdit();
        Selection.Set(Object);
    }
}

void FPicoEditorApp::SetStatus(std::string Message, bool bIsError)
{
    Status = std::move(Message);
    bStatusIsError = bIsError;
}

void FPicoEditorApp::ApplyCommandResult(FEditorCommandResult Result)
{
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
}
}
