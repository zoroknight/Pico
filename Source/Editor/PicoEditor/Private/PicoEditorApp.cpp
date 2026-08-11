#include "PicoEditorApp.h"
#include "NativeFileDialog.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/PlatformProcess.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
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
#include <ImGuizmo.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
enum EDocumentAction
{
    DocumentActionNone,
    DocumentActionNew,
    DocumentActionOpen,
    DocumentActionExit
};

bool DrawPlayStopButton(bool bGameRunning)
{
    const float Size = ImGui::GetFrameHeight();
    const char* Id = bGameRunning ? "##StopGame" : "##PlayGame";
    const bool bPressed = ImGui::InvisibleButton(Id, ImVec2(Size, Size));
    const bool bHovered = ImGui::IsItemHovered();
    const bool bHeld = ImGui::IsItemActive();

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();
    const ImU32 Background = ImGui::GetColorU32(
        bHeld ? ImGuiCol_ButtonActive
              : (bHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
    DrawList->AddRectFilled(Min, Max, Background, ImGui::GetStyle().FrameRounding);

    const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
    if (bGameRunning)
    {
        const float HalfExtent = Size * 0.23f;
        DrawList->AddRectFilled(
            ImVec2(Center.x - HalfExtent, Center.y - HalfExtent),
            ImVec2(Center.x + HalfExtent, Center.y + HalfExtent),
            IM_COL32(232, 67, 62, 255),
            1.0f);
    }
    else
    {
        const float HalfHeight = Size * 0.24f;
        const float HalfWidth = Size * 0.20f;
        DrawList->AddTriangleFilled(
            ImVec2(Center.x - HalfWidth, Center.y - HalfHeight),
            ImVec2(Center.x - HalfWidth, Center.y + HalfHeight),
            ImVec2(Center.x + HalfWidth * 1.35f, Center.y),
            IM_COL32(62, 207, 104, 255));
    }
    return bPressed;
}

void BuildDefaultDockLayout(ImGuiID DockspaceId, const ImVec2& DockspaceSize)
{
    ImGui::DockBuilderRemoveNode(DockspaceId);
    ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockspaceId, DockspaceSize);

    ImGuiID CenterNodeId = DockspaceId;
    ImGuiID OutlinerNodeId = 0;
    ImGuiID DetailsNodeId = 0;
    ImGuiID ContentBrowserNodeId = 0;
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
    ImGui::DockBuilderSplitNode(
        CenterNodeId,
        ImGuiDir_Down,
        0.30f,
        &ContentBrowserNodeId,
        &CenterNodeId);

    ImGui::DockBuilderDockWindow("Scene Outliner", OutlinerNodeId);
    ImGui::DockBuilderDockWindow("Viewport", CenterNodeId);
    ImGui::DockBuilderDockWindow("Details", DetailsNodeId);
    ImGui::DockBuilderDockWindow("Content Browser", ContentBrowserNodeId);
    ImGui::DockBuilderDockWindow("Message Log", ContentBrowserNodeId);
    ImGui::DockBuilderFinish(DockspaceId);
}

}

FPicoEditorApp::FPicoEditorApp(
    FEngineLoop* InEngineLoop,
    FSceneViewportRenderer* InViewportRenderer,
    GLFWwindow* InWindow)
    : EngineLoop(InEngineLoop)
    , Window(InWindow)
    , WorldDocument(InEngineLoop)
    , AssetService(InEngineLoop)
    , CommandService(
        InEngineLoop,
        &Selection,
        &TransactionManager,
        &SceneClipboard,
        [this]() { WorldDocument.MarkDirty(); })
    , PropertyService(
        InEngineLoop,
        &Selection,
        &TransactionManager,
        [this](
            const FEditorWorldSnapshot& Snapshot,
            EWorldSerializationError* Error)
        {
            return RestoreEditorSnapshot(Snapshot, Error);
        })
    , ViewportPanel(InViewportRenderer, InWindow)
    , SkeletalAssetEditor(
        InEngineLoop,
        [this](std::string Message, bool bError)
        {
            SetStatus(std::move(Message), bError);
        },
        [this](const FAssetPath& AssetPath)
        {
            AssetSelection.Select(AssetPath);
            ContentBrowserPanel.FocusAsset(
                EngineLoop->GetAssetRegistry(), AssetSelection, AssetPath);
        })
    , AssetWorkflow(
        InEngineLoop,
        &AssetService,
        &CommandService,
        &AssetSelection,
        [this](std::string Message, bool bError)
        {
            SetStatus(std::move(Message), bError);
        },
        [this](const FAssetPath& AssetPath)
        {
            ViewportPanel.InvalidateStaticMesh(AssetPath);
        })
{
    FConfigFile Config;
    const std::filesystem::path ConfigPath = FPaths::GetProjectConfigFile("Pico.ini");
    const bool bHasConfig = Config.Load(ConfigPath);
    const std::string StartupMap = bHasConfig
        ? Config.GetString(
            "Editor",
            "StartupMap",
            Config.GetString("Game", "DefaultMap", ""))
        : std::string {};
    FAssetPath StartupAssetPath;
    if (FAssetPath::TryParse(StartupMap, StartupAssetPath))
    {
        FEditorDocumentResult OpenResult = WorldDocument.Open(StartupAssetPath);
        SetStatus(std::move(OpenResult.Message), !OpenResult.bSucceeded);
        if (OpenResult.bSucceeded)
        {
            FinishDocumentChange();
        }
        else
        {
            Select(GetWorld());
        }
    }
    else
    {
        PWorld* World = GetWorld();
        Select(World);
        SetStatus(
            World != nullptr ? "New editor World is ready" : "No active editor World",
            World == nullptr);
    }
    UpdateWindowTitle();
}

FPicoEditorApp::~FPicoEditorApp()
{
    StopGame(false);
}

void FPicoEditorApp::Draw()
{
    UpdateGameProcess();
    bInteractiveEditVisited = false;
    ImGuizmo::BeginFrame();
    HandleShortcuts();

    if (!Selection.Validate())
    {
        CancelInteractiveEdit();
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
        DrawViewMenu();
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
    DrawUnsavedChangesPopup();
    DrawPlayValidationPopup();
    ImGui::End();

    if (ImGui::Begin("Scene Outliner"))
    {
        OutlinerPanel.Draw(
            GetWorld(),
            Selection,
            CommandService,
            CommandQueue,
            [this](
                PObject* Object,
                EEditorSelectionOperation Operation,
                const std::vector<PObject*>& OrderedObjects)
            {
                Select(Object, Operation, OrderedObjects);
            },
            [this](PObject* Object) { BeginRename(Object); },
            [this](FEditorCommandResult Result) { ApplyCommandResult(std::move(Result)); });
    }
    ImGui::End();

    DrawMessageLog();

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
            },
            EngineLoop->GetAssetRegistry(),
            [this](const FAssetPath& AssetPath)
            {
                const bool bFocused = ContentBrowserPanel.FocusAsset(
                    EngineLoop->GetAssetRegistry(),
                    AssetSelection,
                    AssetPath);
                SetStatus(
                    bFocused ? "Focused asset in Content Browser"
                             : "Asset is not registered",
                    !bFocused);
            },
            AssetSelection.GetSelectedPath(),
            [this](
                FObjectHandle ObjectHandle,
                FName PropertyName,
                const FAssetPath& AssetPath)
            {
                CommandQueue.Enqueue(
                    [this, ObjectHandle, PropertyName, AssetPath]()
                    {
                        FinishInteractiveEdit();
                        FEditorPropertyResult Result = PropertyService.SetProperty(
                            ObjectHandle,
                            PropertyName,
                            AssetPath);
                        if (Result.bSucceeded)
                        {
                            WorldDocument.MarkDirty();
                        }
                        SetStatus(std::move(Result.Message), !Result.bSucceeded);
                    });
            });
    }
    ImGui::End();

    if (ImGui::Begin("Content Browser"))
    {
        ContentBrowserPanel.Draw(
            EngineLoop->GetAssetRegistry(),
            AssetSelection,
            [this]() { AssetWorkflow.OpenImport(); },
            [this]() { SkeletalAssetEditor.OpenImport(); },
            [this]() { AssetWorkflow.OpenTextureImport(); },
            [this]() { AssetWorkflow.OpenCreateMaterial(); },
            [this]() { AssetWorkflow.RefreshRegistry(); },
            [this](const FAssetPath& AssetPath)
            {
                const FAssetPath StablePath = AssetPath;
                CommandQueue.Enqueue(
                    [this, StablePath]() { AssetWorkflow.Reimport(StablePath); });
            },
            [this](const FAssetPath& AssetPath)
            {
                AssetWorkflow.OpenReimportOptions(AssetPath);
            },
            [this](const std::vector<FAssetPath>& AssetPaths)
            {
                AssetWorkflow.OpenDelete(AssetPaths);
            },
            [this](const FAssetPath& AssetPath)
            {
                AssetWorkflow.OpenRename(AssetPath);
            },
            [this](const FAssetPath& AssetPath)
            {
                AssetWorkflow.OpenEditMaterial(AssetPath);
            },
            [this](const FAssetPath& AssetPath)
            {
                SkeletalAssetEditor.OpenAsset(AssetPath);
            },
            [this](const FAssetPath& AssetPath)
            {
                const FAssetPath StablePath = AssetPath;
                CommandQueue.Enqueue(
                    [this, StablePath]()
                    {
                        FinishInteractiveEdit();
                        PendingWorldAssetPath = StablePath;
                        RequestDocumentAction(DocumentActionOpen);
                    });
            },
            [this](const FAssetPath& AssetPath) { CreateStaticMeshActor(AssetPath); },
            [this](const FAssetPath& AssetPath) { AssignSelectedAsset(AssetPath); },
            [this](const FAssetPath& AssetPath)
            {
                return AssetWorkflow.CanReimport(AssetPath);
            });
    }
    ImGui::End();

    AssetWorkflow.Draw();
    SkeletalAssetEditor.Draw();

    if (bCancelInteractiveEditRequested)
    {
        bCancelInteractiveEditRequested = false;
        bFinishInteractiveEditRequested = false;
        CancelInteractiveEdit();
    }
    else if (bFinishInteractiveEditRequested
        || (!InteractiveEditKey.empty() && !bInteractiveEditVisited))
    {
        bFinishInteractiveEditRequested = false;
        FinishInteractiveEdit();
    }

    ProcessDeferredActions();
    UpdateWindowTitle();
}

void FPicoEditorApp::RequestClose()
{
    RequestDocumentAction(DocumentActionExit);
}

bool FPicoEditorApp::ShouldClose() const
{
    return bShouldClose;
}

void FPicoEditorApp::DrawViewport(float Width, float Height)
{
    ViewportPanel.Draw(
        GetWorld(),
        EngineLoop->GetAssetRegistry(),
        EngineLoop->GetAssetManager(),
        Selection,
        ToolState,
        TransformService,
        Width,
        Height,
        bPreviewSceneCamera,
        [this](
            PObject* Object,
            EEditorSelectionOperation Operation,
            const std::vector<PObject*>& OrderedObjects)
        {
            Select(Object, Operation, OrderedObjects);
        },
        [this](std::string Message) { SetStatus(std::move(Message)); },
        [this](std::string Description)
        {
            FinishInteractiveEdit();
            return BeginEditorTransaction(std::move(Description));
        },
        [this](bool bCommit)
        {
            if (bCommit)
            {
                CommitEditorTransaction();
            }
            else
            {
                CancelEditorTransaction();
            }
        });
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

    const auto DrawToolButton = [this](
        const char* Label,
        const char* Tooltip,
        EEditorTransformMode Mode)
    {
        const bool bSelected = ToolState.TransformMode == Mode;
        if (bSelected)
        {
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(Label, ImVec2(28.0f, 0.0f)))
        {
            ToolState.TransformMode = Mode;
        }
        if (bSelected)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", Tooltip);
        }
    };

    const bool bGameRunning = GameProcess.IsValid();
    if (DrawPlayStopButton(bGameRunning))
    {
        if (bGameRunning)
        {
            StopGame();
        }
        else
        {
            StartGame();
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            bGameRunning
                ? "Stop the standalone game"
                : "Save the World and play in a standalone game window");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::BeginDisabled(ViewportPanel.IsTransformActive());
    DrawToolButton("Q", "Select (Q)", EEditorTransformMode::Select);
    ImGui::SameLine(0.0f, 2.0f);
    DrawToolButton("W", "Translate (W)", EEditorTransformMode::Translate);
    ImGui::SameLine(0.0f, 2.0f);
    DrawToolButton("E", "Rotate (E)", EEditorTransformMode::Rotate);
    ImGui::SameLine(0.0f, 2.0f);
    DrawToolButton("R", "Scale (R)", EEditorTransformMode::Scale);
    ImGui::SameLine();
    if (ImGui::Button(
            ToolState.CoordinateSpace == EEditorCoordinateSpace::World
                ? "World" : "Local"))
    {
        ToolState.CoordinateSpace =
            ToolState.CoordinateSpace == EEditorCoordinateSpace::World
            ? EEditorCoordinateSpace::Local
            : EEditorCoordinateSpace::World;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Toggle world/local coordinates");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &ToolState.bSnapEnabled);
    ImGui::EndDisabled();

    ImGui::SameLine();
    FSceneView SceneCameraView;
    const bool bHasActiveSceneCamera =
        TryBuildActiveCameraView(GetWorld(), SceneCameraView);
    if (!bHasActiveSceneCamera)
    {
        bPreviewSceneCamera = false;
    }
    ImGui::BeginDisabled(!bHasActiveSceneCamera);
    ImGui::Checkbox("Scene Camera", &bPreviewSceneCamera);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(
            bHasActiveSceneCamera
                ? "Preview the first active Camera Component"
                : "Add an active Camera Component to preview it");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

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
        if (ImGui::MenuItem("Player Start"))
        {
            SpawnPlayerStart();
        }
        if (ImGui::MenuItem("Camera"))
        {
            SpawnComponentActor(EEditorSceneComponentType::Camera);
        }
        if (ImGui::MenuItem("Spring Arm"))
        {
            SpawnComponentActor(EEditorSceneComponentType::SpringArm);
        }
        if (ImGui::MenuItem("Directional Light"))
        {
            SpawnComponentActor(EEditorSceneComponentType::DirectionalLight);
        }
        if (ImGui::MenuItem("Point Light"))
        {
            SpawnComponentActor(EEditorSceneComponentType::PointLight);
        }
        const FAssetPath* StaticMeshAsset = GetSelectedStaticMeshAsset();
        if (ImGui::MenuItem("Static Mesh", nullptr, false, StaticMeshAsset != nullptr))
        {
            SpawnStaticMeshActor();
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
        if (ImGui::MenuItem("Camera Component"))
        {
            AddComponentToSelection(EEditorSceneComponentType::Camera);
        }
        if (ImGui::MenuItem("Spring Arm Component"))
        {
            AddComponentToSelection(EEditorSceneComponentType::SpringArm);
        }
        if (ImGui::MenuItem("Directional Light Component"))
        {
            AddComponentToSelection(EEditorSceneComponentType::DirectionalLight);
        }
        if (ImGui::MenuItem("Point Light Component"))
        {
            AddComponentToSelection(EEditorSceneComponentType::PointLight);
        }
        if (ImGui::MenuItem("Skeletal Mesh Component"))
        {
            AddComponentToSelection(EEditorSceneComponentType::SkeletalMesh);
        }
        const FAssetPath* StaticMeshAsset = GetSelectedStaticMeshAsset();
        if (ImGui::MenuItem(
                "Static Mesh Component",
                nullptr,
                false,
                StaticMeshAsset != nullptr))
        {
            AddStaticMeshComponentToSelection();
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

    if (ViewportPanel.IsTransformActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ViewportPanel.CancelActiveTransform();
            CancelEditorTransaction();
            SetStatus("Transform cancelled");
        }
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
    else if (IO.KeyCtrl && IO.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveWorldAs();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveWorld();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        OpenWorld();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
    {
        NewWorld();
    }
    else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
    {
        if (ContentBrowserPanel.IsKeyboardFocused())
        {
            ContentBrowserPanel.SelectAllVisible(
                EngineLoop->GetAssetRegistry(), AssetSelection);
            SetStatus(
                "Selected " + std::to_string(AssetSelection.Num()) + " asset(s)");
        }
        else
        {
            SelectAllActors();
        }
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        if (ContentBrowserPanel.IsKeyboardFocused() && AssetSelection.Num() > 0)
        {
            AssetWorkflow.OpenDelete(AssetSelection.GetSelectedPaths());
        }
        else
        {
            DestroySelectedObject();
        }
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        if (ContentBrowserPanel.IsKeyboardFocused())
        {
            if (AssetSelection.Num() == 1)
            {
                AssetWorkflow.OpenRename(AssetSelection.GetSelectedPath());
            }
            else
            {
                SetStatus("Select exactly one asset to rename", true);
            }
        }
        else
        {
            RenameSelectedObject();
        }
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        const bool bFocused = ViewportPanel.FocusSelection(
            EngineLoop->GetAssetRegistry(),
            EngineLoop->GetAssetManager(),
            Selection);
        SetStatus(bFocused ? "Focused selection" : "Selection has no scene bounds", !bFocused);
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (ContentBrowserPanel.IsKeyboardFocused())
        {
            AssetSelection.Clear();
            SetStatus("Cleared asset selection");
        }
        else
        {
            Select(nullptr);
            SetStatus("Cleared selection");
        }
    }
    else if (!IO.KeyCtrl && !IO.KeyAlt && !ViewportPanel.IsCameraCaptured())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
        {
            ToolState.TransformMode = EEditorTransformMode::Select;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            ToolState.TransformMode = EEditorTransformMode::Translate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            ToolState.TransformMode = EEditorTransformMode::Rotate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            ToolState.TransformMode = EEditorTransformMode::Scale;
        }
    }
}

void FPicoEditorApp::DrawFileMenu()
{
    if (!ImGui::BeginMenu("File"))
    {
        return;
    }

    if (ImGui::MenuItem("New World", "Ctrl+N"))
    {
        NewWorld();
    }
    if (ImGui::MenuItem("Open World...", "Ctrl+O"))
    {
        OpenWorld();
    }
    if (ImGui::MenuItem("Save World", "Ctrl+S"))
    {
        SaveWorld();
    }
    if (ImGui::MenuItem("Save World As...", "Ctrl+Shift+S"))
    {
        SaveWorldAs();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit"))
    {
        RequestClose();
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
    if (ImGui::MenuItem("Select All Actors", "Ctrl+A", false, GetWorld() != nullptr))
    {
        SelectAllActors();
    }
    if (ImGui::MenuItem("Clear Selection", "Esc", false, Selection.IsValid()))
    {
        Select(nullptr);
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

void FPicoEditorApp::DrawViewMenu()
{
    if (!ImGui::BeginMenu("View"))
    {
        return;
    }
    ImGui::MenuItem("Message Log", nullptr, &bMessageLogOpen);
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
        : (bStatusIsWarning
            ? ImVec4(0.95f, 0.76f, 0.28f, 1.0f)
            : ImVec4(0.35f, 0.78f, 0.66f, 1.0f));
    ImGui::TextColored(Color, "%s", Status.c_str());
}

void FPicoEditorApp::DrawMessageLog()
{
    if (!bMessageLogOpen)
    {
        return;
    }
    if (bFocusMessageLog)
    {
        ImGui::SetNextWindowFocus();
        bFocusMessageLog = false;
    }
    if (ImGui::Begin("Message Log", &bMessageLogOpen))
    {
        if (ImGui::Button("Clear"))
        {
            Messages.clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu message(s)", Messages.size());
        ImGui::Separator();
        if (ImGui::BeginChild(
                "MessageLogEntries",
                ImVec2(0.0f, 0.0f),
                false,
                ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const FEditorMessage& Message : Messages)
            {
                const ImVec4 Color = Message.Severity == EMessageSeverity::Error
                    ? ImVec4(0.95f, 0.42f, 0.35f, 1.0f)
                    : (Message.Severity == EMessageSeverity::Warning
                        ? ImVec4(0.95f, 0.76f, 0.28f, 1.0f)
                        : ImVec4(0.35f, 0.78f, 0.66f, 1.0f));
                const char* Prefix = Message.Severity == EMessageSeverity::Error
                    ? "[Error] "
                    : (Message.Severity == EMessageSeverity::Warning
                        ? "[Warning] " : "[Info] ");
                ImGui::TextColored(Color, "%s%s", Prefix, Message.Text.c_str());
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void FPicoEditorApp::DrawPlayValidationPopup()
{
    if (bOpenPlayValidationPopup)
    {
        ImGui::OpenPopup("Play Validation");
        bOpenPlayValidationPopup = false;
    }
    if (!ImGui::BeginPopupModal(
            "Play Validation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    const ImVec4 Color = bPendingPlayBlocked
        ? ImVec4(0.95f, 0.42f, 0.35f, 1.0f)
        : (bPendingPlayWarning
            ? ImVec4(0.95f, 0.76f, 0.28f, 1.0f)
            : ImVec4(0.35f, 0.78f, 0.66f, 1.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
    ImGui::TextColored(Color, "%s", PendingPlayValidation.c_str());
    ImGui::PopTextWrapPos();

    if (bPendingPlayNeedsSave)
    {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Standalone Play runs in another process. Save the current World "
            "explicitly so that process can load the same scene.");
    }
    ImGui::Separator();

    if (bPendingPlayBlocked)
    {
        if (ImGui::Button("Close", ImVec2(110.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    const char* ConfirmLabel = bPendingPlayNeedsSave
        ? "Save & Play"
        : (bPendingPlayWarning ? "Play Anyway" : "Play");
    if (ImGui::Button(ConfirmLabel, ImVec2(120.0f, 0.0f)))
    {
        if (!bPendingPlayNeedsSave || SaveWorld())
        {
            const std::string Validation = PendingPlayValidation;
            bPendingPlayNeedsSave = false;
            ImGui::CloseCurrentPopup();
            LaunchGame(Validation);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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

void FPicoEditorApp::DrawUnsavedChangesPopup()
{
    if (bOpenUnsavedChangesPopup)
    {
        ImGui::OpenPopup("Unsaved World");
        bOpenUnsavedChangesPopup = false;
    }
    if (!ImGui::BeginPopupModal(
            "Unsaved World", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::Text("Save changes to %s?", WorldDocument.GetDisplayName().c_str());
    ImGui::TextDisabled("Unsaved changes will be lost if you discard them.");
    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(100.0f, 0.0f)))
    {
        const int Action = PendingDocumentAction;
        if (SaveWorld())
        {
            PendingDocumentAction = DocumentActionNone;
            ImGui::CloseCurrentPopup();
            ContinueDocumentAction(Action);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(100.0f, 0.0f)))
    {
        const int Action = PendingDocumentAction;
        PendingDocumentAction = DocumentActionNone;
        ImGui::CloseCurrentPopup();
        ContinueDocumentAction(Action);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
    {
        PendingDocumentAction = DocumentActionNone;
        PendingWorldAssetPath = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FPicoEditorApp::ProcessDeferredActions()
{
    CommandQueue.Flush();
}

void FPicoEditorApp::StartGame()
{
    if (GameProcess.IsValid())
    {
        return;
    }

    FinishInteractiveEdit();
    if (!FPaths::HasProject())
    {
        SetStatus("Play requires an active Pico project", true);
        return;
    }

    const FEditorCommandResult Validation =
        CommandService.ValidateGameplayForPlay();
    if (!Validation.bSucceeded)
    {
        SetStatus(Validation.Message, true);
        PendingPlayValidation = Validation.Message;
        bPendingPlayBlocked = true;
        bPendingPlayWarning = false;
        bPendingPlayNeedsSave = false;
        bOpenPlayValidationPopup = true;
        return;
    }

    const bool bWarning = Validation.Message.rfind("Gameplay warning:", 0) == 0;
    const bool bNeedsSave =
        !WorldDocument.HasAssetPath() || WorldDocument.IsDirty();
    SetStatus(Validation.Message, false, bWarning);
    if (bWarning || bNeedsSave)
    {
        PendingPlayValidation = Validation.Message;
        bPendingPlayBlocked = false;
        bPendingPlayWarning = bWarning;
        bPendingPlayNeedsSave = bNeedsSave;
        bOpenPlayValidationPopup = true;
        return;
    }
    LaunchGame(Validation.Message);
}

void FPicoEditorApp::LaunchGame(const std::string& ValidationMessage)
{

    FConfigFile ProjectConfig;
    ProjectConfig.Load(FPaths::GetProjectConfigFile("Pico.ini"));
    std::filesystem::path GameExecutableName =
        ProjectConfig.GetString("Game", "Executable", "PicoGame");
    if (GameExecutableName.empty()
        || GameExecutableName.has_parent_path()
        || GameExecutableName.filename() != GameExecutableName)
    {
        SetStatus("[Game] Executable must be a file name", true);
        return;
    }
#if defined(_WIN32)
    if (!GameExecutableName.has_extension())
    {
        GameExecutableName += ".exe";
    }
#endif
    const std::filesystem::path GameExecutable =
        FPaths::GetExecutableDir() / GameExecutableName;
    if (!std::filesystem::is_regular_file(GameExecutable))
    {
        SetStatus(
            "Project game executable was not found next to the editor: "
                + GameExecutable.string(),
            true);
        return;
    }

    std::string Error;
    GameProcess = FPlatformProcess::CreateProcess(
        GameExecutable,
        {
            FPaths::GetProjectFile().string(),
            "-map=" + std::string(WorldDocument.GetAssetPath().ToString())
        },
        FPaths::GetEngineRootDir(),
        &Error);
    if (!GameProcess.IsValid())
    {
        SetStatus("Could not start project game: " + Error, true);
        return;
    }
    SetStatus(
        "Playing standalone game (process "
            + std::to_string(GameProcess.GetProcessId()) + "); "
            + ValidationMessage);
}

void FPicoEditorApp::StopGame(bool bUpdateStatus)
{
    if (!GameProcess.IsValid())
    {
        return;
    }

    bool bStopped = true;
    if (FPlatformProcess::IsRunning(GameProcess))
    {
        bStopped = FPlatformProcess::Terminate(GameProcess);
        if (bStopped)
        {
            FPlatformProcess::WaitForExit(GameProcess, 2000);
        }
    }
    GameProcess.Reset();
    if (bUpdateStatus)
    {
        SetStatus(
            bStopped ? "Standalone game stopped" : "Could not stop PicoGame",
            !bStopped);
    }
}

void FPicoEditorApp::UpdateGameProcess()
{
    if (!GameProcess.IsValid() || FPlatformProcess::IsRunning(GameProcess))
    {
        return;
    }

    int ExitCode = 0;
    FPlatformProcess::WaitForExit(GameProcess, 0, &ExitCode);
    GameProcess.Reset();
    SetStatus(
        "Standalone game exited with code " + std::to_string(ExitCode),
        ExitCode != 0);
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

void FPicoEditorApp::SpawnPlayerStart()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SpawnPlayerStart());
}

void FPicoEditorApp::SpawnComponentActor(EEditorSceneComponentType Type)
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SpawnComponentActor(Type));
}

void FPicoEditorApp::SpawnStaticMeshActor()
{
    FinishInteractiveEdit();
    const FAssetPath* AssetPath = GetSelectedStaticMeshAsset();
    ApplyCommandResult(
        AssetPath != nullptr
            ? CommandService.SpawnStaticMeshActor(*AssetPath)
            : FEditorCommandResult { false, "Select a Static Mesh in Content Browser" });
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

void FPicoEditorApp::AddComponentToSelection(EEditorSceneComponentType Type)
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.AddComponent(Type));
}

void FPicoEditorApp::AddSceneComponentToSelection()
{
    AddComponentToSelection(false);
}

void FPicoEditorApp::AddCubeComponentToSelection()
{
    AddComponentToSelection(true);
}

void FPicoEditorApp::AddStaticMeshComponentToSelection()
{
    FinishInteractiveEdit();
    const FAssetPath* AssetPath = GetSelectedStaticMeshAsset();
    ApplyCommandResult(
        AssetPath != nullptr
            ? CommandService.AddStaticMeshComponent(*AssetPath)
            : FEditorCommandResult { false, "Select a Static Mesh in Content Browser" });
}

const FAssetPath* FPicoEditorApp::GetSelectedStaticMeshAsset() const
{
    if (EngineLoop == nullptr)
    {
        return nullptr;
    }
    const FAssetRecord* Record = AssetSelection.Resolve(EngineLoop->GetAssetRegistry());
    return Record != nullptr && Record->Type == EAssetType::StaticMesh
        ? &Record->AssetPath : nullptr;
}

const FAssetPath* FPicoEditorApp::GetSelectedMaterialAsset() const
{
    if (EngineLoop == nullptr) return nullptr;
    const FAssetRecord* Record = AssetSelection.Resolve(EngineLoop->GetAssetRegistry());
    return Record != nullptr && Record->Type == EAssetType::Material
        ? &Record->AssetPath : nullptr;
}


void FPicoEditorApp::CreateStaticMeshActor(const FAssetPath& AssetPath)
{
    AssetSelection.Select(AssetPath);
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SpawnStaticMeshActor(AssetPath));
}

void FPicoEditorApp::AssignStaticMeshAsset(const FAssetPath& AssetPath)
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.AssignStaticMeshAsset(AssetPath));
}

void FPicoEditorApp::AssignMaterialAsset(const FAssetPath& AssetPath)
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.AssignMaterialAsset(AssetPath));
}

void FPicoEditorApp::AssignSelectedAsset(const FAssetPath& AssetPath)
{
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    if (Record != nullptr && Record->Type == EAssetType::Material)
    {
        AssignMaterialAsset(AssetPath);
    }
    else
    {
        AssignStaticMeshAsset(AssetPath);
    }
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

bool FPicoEditorApp::SaveWorld()
{
    FinishInteractiveEdit();
    if (!WorldDocument.HasAssetPath())
    {
        return SaveWorldAs();
    }
    FEditorDocumentResult Result = WorldDocument.Save();
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
    return Result.bSucceeded;
}

bool FPicoEditorApp::SaveWorldAs()
{
    FinishInteractiveEdit();
    const std::filesystem::path MapsDirectory =
        FPaths::GetProjectContentDir() / "Maps";
    const std::optional<std::filesystem::path> SelectedPath = SaveWorldFileDialog(
        MapsDirectory,
        WorldDocument.HasAssetPath()
            ? WorldDocument.GetFilePath().filename()
            : std::filesystem::path("NewWorld.pworld"));
    if (!SelectedPath.has_value())
    {
        SetStatus("Save World was cancelled");
        return false;
    }

    FEditorDocumentResult Result = WorldDocument.SaveAs(*SelectedPath);
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
    return Result.bSucceeded;
}

void FPicoEditorApp::OpenWorld()
{
    FinishInteractiveEdit();
    PendingWorldAssetPath = {};
    RequestDocumentAction(DocumentActionOpen);
}

void FPicoEditorApp::NewWorld()
{
    FinishInteractiveEdit();
    RequestDocumentAction(DocumentActionNew);
}

void FPicoEditorApp::PerformOpenWorld()
{
    FEditorDocumentResult Result;
    if (PendingWorldAssetPath.IsValid())
    {
        const FAssetPath AssetPath = PendingWorldAssetPath;
        PendingWorldAssetPath = {};
        Result = WorldDocument.Open(AssetPath);
    }
    else
    {
        const std::optional<std::filesystem::path> SelectedPath =
            OpenWorldFileDialog(FPaths::GetProjectContentDir() / "Maps");
        if (!SelectedPath.has_value())
        {
            SetStatus("Open World was cancelled");
            return;
        }
        Result = WorldDocument.Open(*SelectedPath);
    }
    if (Result.bSucceeded)
    {
        FinishDocumentChange();
    }
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
}

void FPicoEditorApp::PerformNewWorld()
{
    FEditorDocumentResult Result = WorldDocument.NewWorld();
    if (Result.bSucceeded)
    {
        FinishDocumentChange();
    }
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
}

void FPicoEditorApp::RequestDocumentAction(int Action)
{
    if (WorldDocument.IsDirty())
    {
        PendingDocumentAction = Action;
        bOpenUnsavedChangesPopup = true;
        return;
    }
    ContinueDocumentAction(Action);
}

void FPicoEditorApp::ContinueDocumentAction(int Action)
{
    switch (Action)
    {
    case DocumentActionNew: PerformNewWorld(); break;
    case DocumentActionOpen: PerformOpenWorld(); break;
    case DocumentActionExit: bShouldClose = true; break;
    default: break;
    }
}

void FPicoEditorApp::FinishDocumentChange()
{
    CommandQueue.Clear();
    TransactionManager.Clear();
    RenameObjectHandle = {};
    bOpenRenamePopup = false;
    AssetSelection.Clear();
    Selection.Set(GetWorld());
}

void FPicoEditorApp::UpdateWindowTitle()
{
    if (Window == nullptr)
    {
        return;
    }
    const std::string NewTitle = WorldDocument.GetDisplayName() + " - Pico Editor";
    if (NewTitle != WindowTitle)
    {
        WindowTitle = NewTitle;
        glfwSetWindowTitle(Window, WindowTitle.c_str());
    }
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
            bCancelInteractiveEditRequested = true;
            SetStatus("Could not apply editor property change", true);
            return;
        }
        bInteractiveEditChanged = true;
    }

    if (!bActive)
    {
        bFinishInteractiveEditRequested = true;
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
    bFinishInteractiveEditRequested = false;

    if (bShouldCommit)
    {
        CommitEditorTransaction();
    }
    else
    {
        TransactionManager.Cancel();
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
    bFinishInteractiveEditRequested = false;
    bCancelInteractiveEditRequested = false;
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
            GetSelectedObjectPaths(),
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
            GetSelectedObjectPaths(),
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
    WorldDocument.MarkDirty();
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
        || !EngineLoop->ReplaceWorld(
            Snapshot.WorldData,
            OutError,
            {EPropertyChangeType::UndoRedo}))
    {
        return false;
    }

    CommandQueue.Clear();
    RenameObjectHandle = {};
    bOpenRenamePopup = false;
    Selection.Restore(
        GetWorld(),
        Snapshot.SelectedObjectPaths,
        Snapshot.PrimaryObjectPath);
    return true;
}

std::string FPicoEditorApp::GetSelectedObjectPath() const
{
    return Selection.GetObjectPath();
}

std::vector<std::string> FPicoEditorApp::GetSelectedObjectPaths() const
{
    return Selection.GetObjectPaths();
}

void FPicoEditorApp::Select(
    PObject* Object,
    EEditorSelectionOperation Operation,
    const std::vector<PObject*>& OrderedObjects)
{
    FinishInteractiveEdit();
    switch (Operation)
    {
    case EEditorSelectionOperation::Replace:
        Selection.Set(Object);
        break;
    case EEditorSelectionOperation::Add:
        Selection.Add(Object);
        break;
    case EEditorSelectionOperation::Toggle:
        Selection.Toggle(Object);
        break;
    case EEditorSelectionOperation::RangeReplace:
        Selection.SetRange(OrderedObjects, Object, false);
        break;
    case EEditorSelectionOperation::RangeAdd:
        Selection.SetRange(OrderedObjects, Object, true);
        break;
    }
}

void FPicoEditorApp::SelectAllActors()
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    FinishInteractiveEdit();
    Selection.Clear();
    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (PActor* Actor : Level->GetActors())
        {
            Selection.Add(Actor);
        }
    }
    SetStatus("Selected " + std::to_string(Selection.Num()) + " Actor(s)");
}

void FPicoEditorApp::SetStatus(
    std::string Message,
    bool bIsError,
    bool bIsWarning)
{
    Status = std::move(Message);
    bStatusIsError = bIsError;
    bStatusIsWarning = bIsWarning && !bIsError;
    Messages.push_back({
        bStatusIsError ? EMessageSeverity::Error
            : (bStatusIsWarning ? EMessageSeverity::Warning
                                : EMessageSeverity::Info),
        Status
    });
    if (Messages.size() > 200)
    {
        Messages.erase(Messages.begin(), Messages.begin() + 50);
    }
    if (bStatusIsError || bStatusIsWarning)
    {
        bMessageLogOpen = true;
        bFocusMessageLog = true;
    }
}

void FPicoEditorApp::ApplyCommandResult(FEditorCommandResult Result)
{
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
}
}
