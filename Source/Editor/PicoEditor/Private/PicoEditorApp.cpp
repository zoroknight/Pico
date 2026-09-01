#include "PicoEditorApp.h"
#include "AgentChatWorkspace.h"
#include "ExternalAgentWorkspace.h"
#include "NativeFileDialog.h"

#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/MemoryTracker.h"
#include "Pico/Core/Profiler.h"
#include "Pico/Core/PlatformProcess.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Editor/EditorProjectManager.h"
#include "Pico/Editor/EditorAgentHost.h"
#include "Pico/Editor/EditorRuntimeFreshness.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/ActorBlueprint.h"
#include "Pico/Engine/CameraActor.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Render/SceneViewportRenderer.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectName.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Property.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Pico
{
struct FPackageOperationState
{
    std::mutex Mutex;
    std::condition_variable Condition;
    FEditorAgentPackageCompletion Completion;
    bool bActive = false;
    bool bComplete = false;
    bool bCancelRequested = false;
};

namespace
{
enum EDocumentAction
{
    DocumentActionNone,
    DocumentActionNew,
    DocumentActionOpen,
    DocumentActionOpenProject,
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
    ImGui::DockBuilderDockWindow("AI Chat", DetailsNodeId);
    ImGui::DockBuilderDockWindow("Development Metrics", DetailsNodeId);
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
    , ActorBlueprintEditor(
        InEngineLoop,
        [this](std::string Message, bool bError)
        {
            SetStatus(std::move(Message), bError);
        },
        [this](const FAssetPath& AssetPath)
        {
            const PClass* Class = FindActorBlueprintGeneratedClass(AssetPath);
            ApplyCommandResult(CommandService.SpawnActor(Class));
        },
        [this](const FAssetPath& AssetPath)
        {
            AssetSelection.Select(AssetPath);
            ContentBrowserPanel.FocusAsset(
                EngineLoop->GetAssetRegistry(), AssetSelection, AssetPath);
        },
        [this]() { WorldDocument.MarkDirty(); })
    , PicoGraphEditor(
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
    if (!FProfiler::Get().IsEnabled())
        FProfiler::Get().SetStorageMode(EProfileStorageMode::AggregateOnly);
    FMemoryTracker::Get().SetEnabled(true);
    if (!TaskSystem.Initialize())
    {
        throw std::runtime_error("Pico task system initialization failed");
    }
    FPaths::TryGetProjectWritePath(
        EProjectWriteRoot::Saved,
        "Editor/PlaySettings.ini",
        PlaySettingsFile);
    LoadPlaySettings();

    FConfigFile Config;
    const std::filesystem::path ConfigPath = FPaths::GetProjectConfigFile("Pico.ini");
    const bool bHasConfig = Config.Load(ConfigPath);
    const std::string StartupMap = bHasConfig
        ? Config.GetString(
            "Editor",
            "StartupMap",
            Config.GetString("Game", "DefaultMap", ""))
        : std::string {};
    std::filesystem::path SessionPath;
    FEditorSessionState Session;
    const bool bHasSessionPath = FPaths::TryGetProjectWritePath(
        EProjectWriteRoot::Saved,
        "Editor/EditorSession.ini",
        SessionPath);
    const bool bHasSession = bHasSessionPath && Session.Load(SessionPath);
    if (bHasSession)
    {
        bAgentChatOpen = Session.bAgentChatOpen;
        bExternalAgentsOpen = Session.bExternalAgentsOpen;
    }
    bool bOpenedWorld = false;
    if (bHasSession && Session.LastWorld.IsValid())
    {
        FEditorDocumentResult OpenResult = WorldDocument.Open(Session.LastWorld);
        bOpenedWorld = OpenResult.bSucceeded;
        SetStatus(
            bOpenedWorld
                ? "Restored editor session World "
                    + std::string(Session.LastWorld.ToString())
                : "Could not restore the previous World; using StartupMap",
            false,
            !bOpenedWorld);
        if (OpenResult.bSucceeded)
        {
            FinishDocumentChange();
        }
    }
    FAssetPath StartupAssetPath;
    if (!bOpenedWorld && FAssetPath::TryParse(StartupMap, StartupAssetPath))
    {
        FEditorDocumentResult OpenResult = WorldDocument.Open(StartupAssetPath);
        bOpenedWorld = OpenResult.bSucceeded;
        SetStatus(std::move(OpenResult.Message), !OpenResult.bSucceeded);
        if (bOpenedWorld)
        {
            FinishDocumentChange();
        }
    }
    if (!bOpenedWorld)
    {
        PWorld* World = GetWorld();
        Select(World);
        SetStatus(
            World != nullptr ? "New editor World is ready" : "No active editor World",
            World == nullptr);
    }
    if (bHasSession && Session.OpenSkeletalAsset.IsValid())
    {
        SkeletalAssetEditor.OpenAsset(Session.OpenSkeletalAsset);
    }
    if (bHasSession && Session.OpenActorBlueprint.IsValid())
    {
        ActorBlueprintEditor.OpenAsset(Session.OpenActorBlueprint);
    }
    SaveEditorSession(true);
    UpdateWindowTitle();
    PackageOperationState = std::make_shared<FPackageOperationState>();
    AgentHost = std::make_unique<FEditorAgentHost>(
        EngineLoop,
        &Selection,
        &TransactionManager,
        &GameThreadDispatcher,
        [this]() { WorldDocument.MarkDirty(); },
        &CommandService,
        &WorldDocument,
        [this](const std::filesystem::path& OutputRoot,
               const std::string& PackageName,
               bool bSmokeTest)
        {
            if (PackageProcess.IsValid())
                return std::pair<bool, std::string> {
                    false, "A package operation is already running"};
            {
                std::lock_guard Lock(PackageOperationState->Mutex);
                PackageOperationState->Completion = {};
                PackageOperationState->bActive = true;
                PackageOperationState->bComplete = false;
                PackageOperationState->bCancelRequested = false;
            }
            std::snprintf(PackageOutputRootSetting.data(),
                PackageOutputRootSetting.size(), "%s", OutputRoot.string().c_str());
            std::snprintf(PackageNameSetting.data(), PackageNameSetting.size(),
                "%s", PackageName.c_str());
            bPackageSmokeTest = bSmokeTest;
            StartPackageProject();
            if (!PackageProcess.IsValid())
            {
                std::lock_guard Lock(PackageOperationState->Mutex);
                PackageOperationState->Completion.Message = Status;
                PackageOperationState->bComplete = true;
                PackageOperationState->bActive = false;
                PackageOperationState->Condition.notify_all();
            }
            return std::pair<bool, std::string> {
                PackageProcess.IsValid(),
                PackageProcess.IsValid()
                    ? "Packaging started: " + PackageOutputDirectory.string()
                     : Status};
        },
        [State = PackageOperationState](
            const FCancellationToken* CancellationToken)
        {
            std::unique_lock Lock(State->Mutex);
            while (!State->bComplete)
            {
                if (CancellationToken
                    && CancellationToken->IsCancellationRequested())
                {
                    State->bCancelRequested = true;
                    State->Condition.notify_all();
                }
                State->Condition.wait_for(
                    Lock, std::chrono::milliseconds(25));
            }
            return State->Completion;
        },
        [this]()
        {
            if (PlaySession.IsActive())
                return std::pair<bool, std::string> {
                    false, "A Play Session is already active"};
            FinishInteractiveEdit();
            if (!FPaths::HasProject())
                return std::pair<bool, std::string> {
                    false, "Play requires an active Pico project"};
            const FEditorCommandResult Validation =
                CommandService.ValidateGameplayForPlay();
            if (!Validation.bSucceeded)
                return std::pair<bool, std::string> {false, Validation.Message};
            if (!WorldDocument.HasAssetPath() || WorldDocument.IsDirty())
                return std::pair<bool, std::string> {
                    false, "Save the active World before starting Play"};
            LaunchGame(Validation.Message);
            return std::pair<bool, std::string> {
                PlaySession.IsActive(),
                PlaySession.IsActive()
                    ? "Play Session started with "
                        + std::to_string(PlaySession.GetProcessCount())
                        + " process(es); it remains active until Stop"
                    : Status};
        },
        [this]()
        {
            if (!PlaySession.IsActive())
                return std::pair<bool, std::string> {
                    false, "No Play Session is active"};
            StopGame(true);
            return std::pair<bool, std::string> {
                !PlaySession.IsActive(),
                !PlaySession.IsActive()
                    ? "Play Session stopped"
                    : "One or more Play processes could not be stopped"};
        },
        [this](const FEditorWorldSnapshot& Snapshot,
               EWorldSerializationError* Error)
        {
            return RestoreEditorSnapshot(Snapshot, Error);
        });
    AgentChatWorkspace = std::make_unique<FAgentChatWorkspace>(
        AgentHost.get(), &TaskSystem, &GameThreadDispatcher,
        [this](const std::filesystem::path& ProjectFile)
        {
            const FEditorProjectResolution Resolution =
                ResolveEditorProjectPath(ProjectFile);
            if (!Resolution.IsResolved())
            {
                SetStatus("Could not open Agent-created project: "
                    + Resolution.Message, true);
                return;
            }
            PendingProjectFile = Resolution.ProjectFile;
            SetStatus("Agent created the project; preparing a clean editor handoff");
            RequestDocumentAction(DocumentActionOpenProject);
        });
    ExternalAgentWorkspace =
        std::make_unique<FExternalAgentWorkspace>(AgentHost.get());
}

FPicoEditorApp::~FPicoEditorApp()
{
    if (PackageOperationState)
    {
        std::lock_guard Lock(PackageOperationState->Mutex);
        if (!PackageOperationState->bComplete)
        {
            PackageOperationState->Completion.Message =
                "Editor closed before packaging completed";
            PackageOperationState->bComplete = true;
            PackageOperationState->Condition.notify_all();
        }
    }
    if (ExternalAgentWorkspace) ExternalAgentWorkspace->Shutdown();
    if (AgentChatWorkspace) AgentChatWorkspace->Shutdown();
    if (AgentHost) AgentHost->Shutdown();
    TaskSystem.Shutdown();
    GameThreadDispatcher.Shutdown();
    ExternalAgentWorkspace.reset();
    AgentChatWorkspace.reset();
    AgentHost.reset();
    SaveEditorSession(true);
    StopGame(false);
    PackageProcess.Reset();
    PackageProcessGroup.Reset();
}

void FPicoEditorApp::PumpGameThreadTasks()
{
    GameThreadDispatcher.Pump();
}

void FPicoEditorApp::Draw()
{
    PumpCoreLogMessages();
    UpdatePlaySession();
    UpdatePackageProcess();
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
        if (bShowFrameRate && EngineLoop != nullptr)
        {
            ImGui::TextDisabled(
                "FPS %.1f  %.2f ms",
                EngineLoop->GetAverageFPS(),
                EngineLoop->GetAverageFrameTimeMS());
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
        }
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
    DrawActorClassPicker();
    DrawPlayableCharacterCreator();
    DrawPackageProjectPopup();
    DrawProjectSettings();
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
    DrawDevelopmentMetrics();

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
            [this]() { ActorBlueprintEditor.OpenCreate(); },
            [this]() { PicoGraphEditor.OpenCreate(); },
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
                ActorBlueprintEditor.OpenAsset(AssetPath);
            },
            [this](const FAssetPath& AssetPath)
            {
                PicoGraphEditor.OpenAsset(AssetPath);
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
    ActorBlueprintEditor.Draw();
    PicoGraphEditor.Draw();
    if (AgentChatWorkspace && bAgentChatOpen)
    {
        AgentChatWorkspace->Draw(&bAgentChatOpen);
    }
    if (ExternalAgentWorkspace && bExternalAgentsOpen)
    {
        ExternalAgentWorkspace->Draw(&bExternalAgentsOpen);
    }
    if (AgentHost) AgentHost->DrawApprovalCenter();

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
    SaveEditorSession();
    UpdateWindowTitle();
}

void FPicoEditorApp::PumpCoreLogMessages()
{
    const std::vector<FLogRecord> Records =
        FLog::GetRecordsSince(LastObservedLogSequence);
    bool bReceivedIssue = false;
    for (const FLogRecord& Record : Records)
    {
        LastObservedLogSequence = std::max(
            LastObservedLogSequence, Record.Sequence);
        if (Record.Level != ELogLevel::Warning
            && Record.Level != ELogLevel::Error)
        {
            continue;
        }
        Messages.push_back({
            Record.Level == ELogLevel::Error
                ? EMessageSeverity::Error : EMessageSeverity::Warning,
            "[" + Record.Category + "] " + Record.Message
        });
        bReceivedIssue = true;
    }
    if (Messages.size() > 200)
    {
        Messages.erase(Messages.begin(), Messages.begin() + (Messages.size() - 200));
    }
    if (bReceivedIssue)
    {
        bMessageLogOpen = true;
        bFocusMessageLog = true;
    }
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

    const bool bGameRunning = PlaySession.IsActive();
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
                ? "Stop every process in the Play Session"
                : "Save the World and start the configured Play Session");
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##PlaySettings", ImGuiDir_Down))
    {
        bOpenPlaySettingsPopup = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Play Session settings");
    }
    DrawPlaySettingsPopup();
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
        if (ImGui::MenuItem("Actor Class..."))
        {
            bOpenActorClassPicker = true;
        }
        if (ImGui::MenuItem("Playable Character..."))
        {
            bOpenPlayableCharacterCreator = true;
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
            FinishInteractiveEdit();
            ApplyCommandResult(
                CommandService.SpawnActor(PCameraActor::StaticClass()));
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
        if (ImGui::MenuItem("Script Component"))
        {
            AddScriptComponentToSelection();
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
    if (PicoGraphEditor.IsKeyboardFocused())
    {
        return;
    }
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

    if (ImGui::MenuItem("Open Project..."))
    {
        OpenProject(false);
    }
    if (ImGui::MenuItem("Open Project Folder..."))
    {
        OpenProject(true);
    }
    ImGui::Separator();
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
    if (ImGui::BeginMenu("Package Project"))
    {
        if (ImGui::MenuItem(
                "Windows (Development)",
                nullptr,
                false,
                !PackageProcess.IsValid()))
        {
            const std::string Output =
                (FPaths::GetProjectSavedDir() / "StagedBuilds").string();
            std::snprintf(
                PackageOutputRootSetting.data(),
                PackageOutputRootSetting.size(),
                "%s",
                Output.c_str());
            FConfigFile ProjectDescriptor;
            ProjectDescriptor.Load(FPaths::GetProjectFile());
            const std::string ProjectName = ProjectDescriptor.GetString(
                "Project", "Name", FPaths::GetProjectFile().stem().string());
            const std::string PackageName =
                ProjectName + "-Windows-Development";
            std::snprintf(
                PackageNameSetting.data(),
                PackageNameSetting.size(),
                "%s",
                PackageName.c_str());
            bOpenPackageProjectPopup = true;
        }
        ImGui::EndMenu();
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
    ImGui::Separator();
    if (ImGui::MenuItem("Project Settings..."))
    {
        bProjectSettingsOpen = true;
        bProjectSettingsLoaded = false;
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
    ImGui::MenuItem("AI Chat", nullptr, &bAgentChatOpen);
    ImGui::MenuItem("External Agents", nullptr, &bExternalAgentsOpen);
    ImGui::MenuItem("Frame Rate", nullptr, &bShowFrameRate);
    ImGui::MenuItem("Development Metrics", nullptr, &bDevelopmentMetricsOpen);
    ImGui::EndMenu();
}

void FPicoEditorApp::DrawDevelopmentMetrics()
{
    if (!bDevelopmentMetricsOpen || EngineLoop == nullptr) return;
    if (!ImGui::Begin("Development Metrics", &bDevelopmentMetricsOpen))
    {
        ImGui::End();
        return;
    }

    const FFrameTimeStatistics Frame = EngineLoop->GetFrameTimeStatistics();
    ImGui::Text("Frame P50 %.2f ms | P95 %.2f ms | P99 %.2f ms",
        Frame.P50Milliseconds, Frame.P95Milliseconds, Frame.P99Milliseconds);
    ImGui::Text("Long frames (>16.67 ms): %llu | samples: %zu",
        static_cast<unsigned long long>(Frame.LongFrameCount), Frame.SampleCount);

    PWorld* World = EngineLoop->GetWorld();
    const std::size_t TickCount = World != nullptr
        ? World->GetTickTaskManager().GetRegisteredTickFunctionCount() : 0;
    std::uint64_t ScheduleBuilds = 0;
    if (World != nullptr)
        for (int Group = 0; Group < 4; ++Group)
            ScheduleBuilds += World->GetTickTaskManager().GetScheduleBuildCount(
                static_cast<ETickGroup>(Group));
    ImGui::Text("Objects %zu | Tick functions %zu | Schedule rebuilds %llu",
        FObjectRegistry::GetObjectCount(), TickCount,
        static_cast<unsigned long long>(ScheduleBuilds));
    ImGui::Text("Task workers %zu | queued %zu",
        TaskSystem.GetWorkerCount(), TaskSystem.GetQueuedTaskCount());

    const FGarbageCollectionResult& GC =
        EngineLoop->GetLastGarbageCollectionResult();
    ImGui::Text("Last GC: before %zu | collected %zu | after %zu | %.3f ms",
        GC.ObjectCountBefore, GC.CollectedObjectCount, GC.ObjectCountAfter,
        static_cast<double>(GC.RootScanNanoseconds + GC.MarkNanoseconds
            + GC.UnreachableSortNanoseconds + GC.DestroyNanoseconds) / 1000000.0);

    if (World != nullptr && World->GetNetDriver() != nullptr)
    {
        const FReplicationStatistics Net =
            World->GetNetDriver()->GetReplicationStatistics();
        ImGui::Text("Replication: actors %llu | dirty %llu | fields %llu | bytes %llu",
            static_cast<unsigned long long>(Net.ActorsConsidered),
            static_cast<unsigned long long>(Net.DirtyActors),
            static_cast<unsigned long long>(Net.DirtyProperties),
            static_cast<unsigned long long>(Net.BytesQueued));
    }
    else ImGui::TextDisabled("Replication: inactive");

    if (ImGui::CollapsingHeader("Top CPU Scopes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        std::vector<FProfileAggregate> Aggregates = FProfiler::Get().GetAggregates();
        std::sort(Aggregates.begin(), Aggregates.end(),
            [](const FProfileAggregate& A, const FProfileAggregate& B)
            { return A.TotalMicroseconds > B.TotalMicroseconds; });
        const std::size_t Count = std::min<std::size_t>(Aggregates.size(), 8);
        for (std::size_t Index = 0; Index < Count; ++Index)
            ImGui::BulletText("%s  %.3f ms | %llu calls",
                Aggregates[Index].Name.c_str(),
                static_cast<double>(Aggregates[Index].TotalMicroseconds) / 1000.0,
                static_cast<unsigned long long>(Aggregates[Index].Count));
    }

    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const FMemorySnapshot& Snapshot : FMemoryTracker::Get().GetSnapshots())
            ImGui::Text("%.*s  current %.2f MB | reserved %.2f MB | peak %.2f MB",
                static_cast<int>(GetMemoryTagName(Snapshot.Tag).size()),
                GetMemoryTagName(Snapshot.Tag).data(),
                static_cast<double>(Snapshot.CurrentBytes) / (1024.0 * 1024.0),
                static_cast<double>(Snapshot.ReservedBytes) / (1024.0 * 1024.0),
                static_cast<double>(Snapshot.PeakBytes) / (1024.0 * 1024.0));
    }

    const FProfilerStorageStats Storage = FProfiler::Get().GetStorageStats();
    ImGui::Separator();
    ImGui::Text("Profiler mode %d | events %zu/%zu | overwritten %llu",
        static_cast<int>(Storage.Mode), Storage.StoredEventCount,
        Storage.TraceCapacity,
        static_cast<unsigned long long>(Storage.DroppedEventCount));
    if (ImGui::Button("Compact At Safe Point"))
    {
        FProfiler::Get().Compact();
        std::string CompactError;
        const bool bCompacted = FObjectRegistry::CompactStorage(&CompactError);
        SetStatus(
            bCompacted
                ? "Runtime storage compacted at editor safe point"
                : "Runtime storage compact failed: " + CompactError,
            !bCompacted);
    }
    ImGui::End();
}

void FPicoEditorApp::LoadProjectSettings()
{
    FConfigFile Config;
    Config.Load(FPaths::GetProjectConfigFile("Pico.ini"));
    const auto CopySetting = [](auto& Buffer, const std::string& Value)
    {
        std::snprintf(Buffer.data(), Buffer.size(), "%s", Value.c_str());
    };
    CopySetting(DefaultMapSetting,
        Config.GetString("Game", "DefaultMap", "/Game/Maps/EditorWorld.pworld"));
    CopySetting(DefaultPawnClassSetting,
        Config.GetString("Game", "DefaultPawnClass", "PSandboxPawn"));
    CopySetting(PlayerControllerClassSetting,
        Config.GetString("Game", "PlayerControllerClass", "PSandboxPlayerController"));
    CopySetting(DefaultPawnProfileSetting,
        Config.GetString("Game", "DefaultPawnProfile", ""));
    MouseSensitivitySetting = static_cast<float>(
        Config.GetDouble("Input", "MouseSensitivity", 0.12));
    InputMappingSettings.clear();
    auto Entries = Config.GetSectionEntries("Input");
    std::sort(Entries.begin(), Entries.end());
    for (const auto& [Key, Value] : Entries)
    {
        const bool bAxis = Key.starts_with("Axis.");
        if (!bAxis && !Key.starts_with("Action.")) continue;
        FInputMappingSetting Mapping;
        Mapping.bAxis = bAxis;
        CopySetting(Mapping.Name, Key.substr(Key.find('.') + 1));
        CopySetting(Mapping.Bindings, Value);
        InputMappingSettings.push_back(Mapping);
    }
    bProjectSettingsLoaded = true;
}

void FPicoEditorApp::SaveProjectSettings()
{
    FConfigFile Config;
    const std::filesystem::path ConfigPath = FPaths::GetProjectConfigFile("Pico.ini");
    Config.Load(ConfigPath);
    Config.SetString("Game", "DefaultMap", DefaultMapSetting.data());
    Config.SetString("Game", "DefaultPawnClass", DefaultPawnClassSetting.data());
    Config.SetString("Game", "PlayerControllerClass", PlayerControllerClassSetting.data());
    Config.SetString("Game", "DefaultPawnProfile", DefaultPawnProfileSetting.data());
    Config.RemoveSection("Input");
    Config.SetString("Input", "MouseSensitivity", std::to_string(MouseSensitivitySetting));
    for (const FInputMappingSetting& Mapping : InputMappingSettings)
    {
        if (Mapping.Name[0] == '\0' || Mapping.Bindings[0] == '\0') continue;
        Config.SetString(
            "Input",
            std::string(Mapping.bAxis ? "Axis." : "Action.") + Mapping.Name.data(),
            Mapping.Bindings.data());
    }
    if (Config.Save(ConfigPath))
        SetStatus("Project settings saved. They apply on the next Play launch.");
    else
        SetStatus("Could not save project settings: " + ConfigPath.string(), true);
}

void FPicoEditorApp::DrawProjectSettings()
{
    if (!bProjectSettingsOpen) return;
    if (!bProjectSettingsLoaded) LoadProjectSettings();
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &bProjectSettingsOpen))
    {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("ProjectSettingsTabs"))
    {
        if (ImGui::BeginTabItem("Input"))
        {
            ImGui::SetNextItemWidth(180.0f);
            ImGui::DragFloat("Mouse Sensitivity", &MouseSensitivitySetting, 0.005f, 0.01f, 5.0f, "%.3f");
            ImGui::Separator();
            ImGui::TextUnformatted("Mappings");
            int RemoveIndex = -1;
            for (std::size_t Index = 0; Index < InputMappingSettings.size(); ++Index)
            {
                FInputMappingSetting& Mapping = InputMappingSettings[Index];
                ImGui::PushID(static_cast<int>(Index));
                ImGui::SetNextItemWidth(90.0f);
                const char* Types[] = {"Action", "Axis"};
                int Type = Mapping.bAxis ? 1 : 0;
                if (ImGui::Combo("##Type", &Type, Types, 2)) Mapping.bAxis = Type == 1;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputText("##Name", Mapping.Name.data(), Mapping.Name.size());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-45.0f);
                ImGui::InputTextWithHint(
                    "##Bindings", Mapping.bAxis ? "W:1,S:-1" : "Space,Enter",
                    Mapping.Bindings.data(), Mapping.Bindings.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) RemoveIndex = static_cast<int>(Index);
                ImGui::PopID();
            }
            if (RemoveIndex >= 0)
                InputMappingSettings.erase(InputMappingSettings.begin() + RemoveIndex);
            if (ImGui::Button("Add Action")) InputMappingSettings.push_back({});
            ImGui::SameLine();
            if (ImGui::Button("Add Axis"))
            {
                FInputMappingSetting Mapping;
                Mapping.bAxis = true;
                InputMappingSettings.push_back(Mapping);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gameplay"))
        {
            const auto DrawAssetSetting = [this](
                const char* Label,
                auto& Buffer,
                EAssetType Type,
                bool bCanClear)
            {
                ImGui::SetNextItemWidth(-1.0f);
                if (!ImGui::BeginCombo(Label, Buffer[0] != '\0' ? Buffer.data() : "None")) return;
                if (bCanClear && ImGui::Selectable("None", Buffer[0] == '\0')) Buffer[0] = '\0';
                if (EngineLoop != nullptr)
                {
                    for (const FAssetRecord& Record : EngineLoop->GetAssetRegistry().GetAssets())
                    {
                        if (Record.Type != Type) continue;
                        const std::string Path(Record.AssetPath.ToString());
                        const bool bSelected = Path == Buffer.data();
                        if (ImGui::Selectable(Path.c_str(), bSelected))
                            std::snprintf(Buffer.data(), Buffer.size(), "%s", Path.c_str());
                    }
                }
                ImGui::EndCombo();
            };
            const auto DrawClassSetting = [](const char* Label, auto& Buffer, const PClass* BaseClass)
            {
                ImGui::SetNextItemWidth(-1.0f);
                if (!ImGui::BeginCombo(Label, Buffer[0] != '\0' ? Buffer.data() : "None")) return;
                auto Classes = FClassRegistry::GetClasses();
                std::sort(Classes.begin(), Classes.end(), [](const PClass* Left, const PClass* Right)
                    { return Left->GetName().ToString() < Right->GetName().ToString(); });
                for (const PClass* Class : Classes)
                {
                    if (Class == nullptr || !Class->CanConstruct() || !Class->IsChildOf(BaseClass)) continue;
                    const std::string Name = Class->GetName().ToString();
                    const bool bSelected = Name == Buffer.data();
                    if (ImGui::Selectable(Name.c_str(), bSelected))
                        std::snprintf(Buffer.data(), Buffer.size(), "%s", Name.c_str());
                }
                ImGui::EndCombo();
            };
            DrawAssetSetting("Default Map", DefaultMapSetting, EAssetType::World, false);
            DrawClassSetting("Default Pawn Class", DefaultPawnClassSetting, PPawn::StaticClass());
            DrawClassSetting("Player Controller Class", PlayerControllerClassSetting,
                PPlayerController::StaticClass());
            DrawAssetSetting("Default Pawn Profile", DefaultPawnProfileSetting,
                EAssetType::CharacterProfile, true);
            ImGui::Spacing();
            ImGui::TextDisabled("Defaults are used only when the map has no Player 0 auto-possessed Pawn.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    if (ImGui::Button("Save Settings")) SaveProjectSettings();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) LoadProjectSettings();
    ImGui::End();
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

void FPicoEditorApp::DrawPlaySettingsPopup()
{
    if (bOpenPlaySettingsPopup)
    {
        ImGui::OpenPopup("Play Session Settings");
        bOpenPlaySettingsPopup = false;
    }
    if (!ImGui::BeginPopup("Play Session Settings")) return;

    bool bChanged = false;
    const char* ModeLabel = PlaySettings.NetMode == EEditorPlayNetMode::Standalone
        ? "Standalone" : "Separate Server + Clients (Visible)";
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::BeginCombo("Play Mode", ModeLabel))
    {
        if (ImGui::Selectable(
                "Standalone",
                PlaySettings.NetMode == EEditorPlayNetMode::Standalone))
        {
            PlaySettings.NetMode = EEditorPlayNetMode::Standalone;
            PlaySettings.PlayerCount = 1;
            bChanged = true;
        }
        if (ImGui::Selectable(
                "Separate Server + Clients (Visible)",
                PlaySettings.NetMode == EEditorPlayNetMode::SeparateServer))
        {
            PlaySettings.NetMode = EEditorPlayNetMode::SeparateServer;
            bChanged = true;
        }
        ImGui::BeginDisabled();
        ImGui::Selectable("Listen Server (Replication milestone)", false);
        ImGui::Selectable("Headless Dedicated Server (Runtime milestone)", false);
        ImGui::EndDisabled();
        ImGui::EndCombo();
    }

    ImGui::BeginDisabled(PlaySettings.NetMode == EEditorPlayNetMode::Standalone);
    bChanged |= ImGui::InputInt("Players", &PlaySettings.PlayerCount);
    bChanged |= ImGui::InputInt("Server Port", &PlaySettings.ServerPort);
    ImGui::EndDisabled();
    bChanged |= ImGui::InputInt("Window Width", &PlaySettings.ClientWindowWidth);
    bChanged |= ImGui::InputInt("Window Height", &PlaySettings.ClientWindowHeight);
    ImGui::Separator();
    ImGui::TextUnformatted("Network Simulation");
    ImGui::BeginDisabled(PlaySettings.NetMode == EEditorPlayNetMode::Standalone);
    bChanged |= ImGui::InputInt(
        "Target RTT (ms)", &PlaySettings.NetworkLatencyMs);
    bChanged |= ImGui::InputInt(
        "Jitter (+/- ms)", &PlaySettings.NetworkJitterMs);
    bChanged |= ImGui::InputInt(
        "Packet Loss (%)", &PlaySettings.PacketLossPercent);
    ImGui::EndDisabled();
    if (bChanged)
    {
        PlaySettings.Clamp();
        SavePlaySettings();
    }

    ImGui::Separator();
    if (PlaySettings.NetMode == EEditorPlayNetMode::Standalone)
    {
        ImGui::TextDisabled("Starts one offline game process.");
    }
    else
    {
        ImGui::TextDisabled(
            "Starts one visible server and %d client process(es).",
            PlaySettings.PlayerCount);
        ImGui::TextDisabled("The server is not included in Players.");
        ImGui::TextDisabled(
            "The server keeps a window until headless runtime is introduced.");
    }
    ImGui::EndPopup();
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
            "Play runs in separate processes. Save the current World "
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
        PendingProjectFile.clear();
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
    if (PlaySession.IsActive())
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
    std::string NewerRuntimeDependency;
    if (IsDevelopmentGameRuntimeStale(
            GameExecutable, NewerRuntimeDependency))
    {
        SetStatus(
            "Project Runtime is older than " + NewerRuntimeDependency
                + ". Build target '" + GameExecutableName.stem().string()
                + "' before Play.",
            true);
        return;
    }
    const auto SessionId = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    FPlaySessionLaunchRequest Request;
    Request.Settings = PlaySettings;
    Request.Executable = GameExecutable;
    Request.ProjectFile = FPaths::GetProjectFile();
    Request.MapAssetPath = std::string(WorldDocument.GetAssetPath().ToString());
    Request.WorkingDirectory = FPaths::GetEngineRootDir();
    Request.LogDirectory = FPaths::GetProjectSavedDir()
        / "Logs" / "PlaySession" / ("Session_" + std::to_string(SessionId));

    std::string Error;
    if (!PlaySession.Start(Request, Error))
    {
        SetStatus("Could not start Play Session: " + Error, true);
        return;
    }
    SetStatus(
        "Started " + std::string(ToString(PlaySettings.NetMode))
            + " Play Session with "
            + std::to_string(PlaySession.GetProcessCount()) + " process(es); "
            + ValidationMessage);
}

void FPicoEditorApp::StopGame(bool bUpdateStatus)
{
    if (!PlaySession.IsActive())
    {
        return;
    }

    const bool bStopped = PlaySession.Stop();
    if (bUpdateStatus)
    {
        SetStatus(
            bStopped ? "Play Session stopped"
                     : "One or more Play processes could not be stopped",
            !bStopped);
    }
}

void FPicoEditorApp::UpdatePlaySession()
{
    const std::vector<FPlayProcessExit> Exits = PlaySession.Poll();
    for (const FPlayProcessExit& Exit : Exits)
    {
        std::string Detail;
        if (Exit.ExitCode != 0 && std::filesystem::is_regular_file(Exit.LogFile))
        {
            std::ifstream Log(Exit.LogFile);
            std::string Line;
            while (std::getline(Log, Line)) if (!Line.empty()) Detail = Line;
        }
        SetStatus(
            Exit.Label + " exited with code " + std::to_string(Exit.ExitCode)
                + (Detail.empty() ? "" : ": " + Detail),
            Exit.ExitCode != 0,
            Exit.ExitCode == 0 && PlaySession.IsActive());
    }
}

void FPicoEditorApp::LoadPlaySettings()
{
    if (!PlaySettingsFile.empty()) PlaySettings.Load(PlaySettingsFile);
    PlaySettings.Clamp();
}

void FPicoEditorApp::SavePlaySettings()
{
    if (!PlaySettingsFile.empty() && !PlaySettings.Save(PlaySettingsFile))
    {
        SetStatus("Could not save Play Session settings", true);
    }
}

void FPicoEditorApp::StartPackageProject()
{
    if (PackageProcess.IsValid())
    {
        SetStatus("A package operation is already running", false, true);
        return;
    }
    if (!FPaths::HasProject())
    {
        SetStatus("Package requires an active Pico project", true);
        return;
    }
    if (!WorldDocument.HasAssetPath() || WorldDocument.IsDirty())
    {
        SetStatus("Save the current World before packaging", false, true);
        return;
    }
    if (PlaySession.IsActive())
    {
        SetStatus("Stop the Play Session before packaging", false, true);
        return;
    }

    FConfigFile ProjectConfig;
    if (!ProjectConfig.Load(FPaths::GetProjectConfigFile("Pico.ini")))
    {
        SetStatus("Could not load project Config/Pico.ini", true);
        return;
    }
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
    if (!GameExecutableName.has_extension()) GameExecutableName += ".exe";
#endif
    const std::filesystem::path BinDirectory = FPaths::GetExecutableDir();
    const std::filesystem::path GameExecutable = BinDirectory / GameExecutableName;
    const std::filesystem::path Receipt =
        BinDirectory / (GameExecutableName.stem().string() + ".targetreceipt");
#if defined(_WIN32)
    const std::filesystem::path Packager = BinDirectory / "PicoPackager.exe";
#else
    const std::filesystem::path Packager = BinDirectory / "PicoPackager";
#endif
    if (!std::filesystem::is_regular_file(Packager))
    {
        SetStatus("PicoPackager was not found next to PicoEditor", true);
        return;
    }
    if (!std::filesystem::is_regular_file(GameExecutable)
        || !std::filesystem::is_regular_file(Receipt))
    {
        SetStatus(
            "Build target '" + GameExecutableName.stem().string()
                + "' before packaging",
            true);
        return;
    }
    PackageProcessLogFile =
        FPaths::GetProjectSavedDir() / "Logs/PackageProject.log";
    const std::filesystem::path OutputRoot(PackageOutputRootSetting.data());
    if (OutputRoot.empty())
    {
        SetStatus("Choose a package output directory", true);
        return;
    }
    const std::string PackageName(PackageNameSetting.data());
    if (PackageName.empty()
        || PackageName.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
            != std::string::npos)
    {
        SetStatus("Package Name may contain only letters, digits, '_' or '-'", true);
        return;
    }
    PackageOutputDirectory = OutputRoot / PackageName;
    std::string Error;
    if (!PackageProcessGroup.InitializeKillOnClose(&Error))
    {
        SetStatus("Could not create package process group: " + Error, true);
        return;
    }
    std::vector<std::string> Arguments {
        "-project=" + FPaths::GetProjectFile().string(),
        "-receipt=" + Receipt.string(),
        "-output=" + OutputRoot.string(),
        "-engineroot=" + FPaths::GetEngineRootDir().string(),
        "-profile=Development",
        "-stagename=" + PackageName
    };
    if (bPackageSmokeTest) Arguments.push_back("-smoke");
    PackageProcess = FPlatformProcess::CreateProcess(
        Packager,
        Arguments,
        FPaths::GetEngineRootDir(),
        PackageProcessLogFile,
        &Error,
        &PackageProcessGroup);
    if (!PackageProcess.IsValid())
    {
        PackageProcessGroup.Reset();
        SetStatus("Could not start PicoPackager: " + Error, true);
        return;
    }
    SetStatus(
        "Packaging Windows Development build (process "
            + std::to_string(PackageProcess.GetProcessId()) + ")");
}

void FPicoEditorApp::DrawPackageProjectPopup()
{
    if (bOpenPackageProjectPopup)
    {
        ImGui::OpenPopup("Package Project");
        bOpenPackageProjectPopup = false;
    }
    if (!ImGui::BeginPopupModal(
            "Package Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::TextUnformatted("Target");
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted("Game");
    ImGui::TextUnformatted("Platform");
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted("Windows");
    ImGui::TextUnformatted("Profile");
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted("Development");
    ImGui::Separator();
    ImGui::SetNextItemWidth(560.0f);
    ImGui::InputText(
        "Output Root",
        PackageOutputRootSetting.data(),
        PackageOutputRootSetting.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        const std::optional<std::filesystem::path> Selected =
            OpenProjectFolderDialog();
        if (Selected.has_value())
        {
            const std::string Path = Selected->string();
            std::snprintf(
                PackageOutputRootSetting.data(),
                PackageOutputRootSetting.size(),
                "%s",
                Path.c_str());
        }
    }
    ImGui::Checkbox("Run two-frame smoke test", &bPackageSmokeTest);
    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputText(
        "Package Name",
        PackageNameSetting.data(),
        PackageNameSetting.size());
    const std::filesystem::path OutputRoot(PackageOutputRootSetting.data());
    const std::filesystem::path FinalOutput =
        OutputRoot / PackageNameSetting.data();
    std::error_code OutputError;
    const bool bReplacingPackage = !OutputRoot.empty()
        && std::filesystem::exists(FinalOutput, OutputError);
    ImGui::TextDisabled("Output: %s", FinalOutput.string().c_str());
    if (bReplacingPackage)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.72f, 0.24f, 1.0f));
        ImGui::TextWrapped(
            "A package with this name already exists. The successful build will replace it.");
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    if (ImGui::Button(
            bReplacingPackage ? "Replace Package" : "Package",
            ImVec2(150.0f, 0.0f)))
    {
        StartPackageProject();
        if (PackageProcess.IsValid()) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void FPicoEditorApp::UpdatePackageProcess()
{
    bool bCancelRequested = false;
    if (PackageOperationState)
    {
        std::lock_guard Lock(PackageOperationState->Mutex);
        bCancelRequested = PackageOperationState->bActive
            && PackageOperationState->bCancelRequested;
    }
    if (PackageProcess.IsValid() && bCancelRequested
        && FPlatformProcess::IsRunning(PackageProcess))
    {
        FPlatformProcess::Terminate(PackageProcess, 1);
    }
    if (!PackageProcess.IsValid()
        || FPlatformProcess::IsRunning(PackageProcess))
    {
        return;
    }
    int ExitCode = 0;
    FPlatformProcess::WaitForExit(PackageProcess, 0, &ExitCode);
    PackageProcess.Reset();
    PackageProcessGroup.Reset();
    std::string Detail;
    if (ExitCode != 0 && std::filesystem::is_regular_file(PackageProcessLogFile))
    {
        std::ifstream Log(PackageProcessLogFile);
        std::string FallbackDetail;
        for (std::string Line; std::getline(Log, Line);)
        {
            if (Line.starts_with("Error:") && Detail.empty())
            {
                Detail = Line.substr(std::string("Error:").size());
                while (!Detail.empty() && Detail.front() == ' ')
                {
                    Detail.erase(Detail.begin());
                }
            }
            else if (!Line.empty() && FallbackDetail.empty())
            {
                FallbackDetail = Line;
            }
        }
        if (Detail.empty()) Detail = std::move(FallbackDetail);
    }
    FConfigFile Report;
    FConfigFile CompletionMarker;
    const bool bCompletionArtifactsValid = ExitCode == 0
        && Report.Load(PackageOutputDirectory / "PackageReport.ini")
        && Report.GetBool("Package", "Succeeded", false)
        && CompletionMarker.Load(
            PackageOutputDirectory / "PicoPackage.complete")
        && CompletionMarker.GetString("Package", "State", "") == "Complete";
    if (ExitCode == 0 && !bCompletionArtifactsValid)
    {
        Detail = "Packager exited successfully, but PackageReport.ini or "
            "PicoPackage.complete did not confirm the output";
    }
    const bool bSucceeded = !bCancelRequested
        && ExitCode == 0 && bCompletionArtifactsValid;
    if (bSucceeded)
    {
        SetStatus("Package succeeded: " + PackageOutputDirectory.string());
    }
    else
    {
        SetStatus(ExitCode == 0
                ? "Package verification failed"
                    + (Detail.empty() ? std::string {} : ": " + Detail)
                : "Package failed with code " + std::to_string(ExitCode)
                    + (Detail.empty() ? "" : ": " + Detail),
            true);
    }
    if (PackageOperationState)
    {
        {
            std::lock_guard Lock(PackageOperationState->Mutex);
            if (PackageOperationState->bActive)
            {
                PackageOperationState->Completion.bSucceeded = bSucceeded;
                PackageOperationState->Completion.ExitCode = ExitCode;
                PackageOperationState->Completion.OutputDirectory =
                    PackageOutputDirectory;
                PackageOperationState->Completion.Message = bCancelRequested
                    ? "Package operation was cancelled"
                    : bSucceeded
                    ? "Package completed and its report and completion marker were verified"
                    : ExitCode == 0
                    ? "Package verification failed"
                        + (Detail.empty() ? std::string {} : ": " + Detail)
                    : "Package failed with code " + std::to_string(ExitCode)
                        + (Detail.empty() ? std::string {} : ": " + Detail);
                PackageOperationState->bComplete = true;
                PackageOperationState->bActive = false;
            }
        }
        PackageOperationState->Condition.notify_all();
    }
}

void FPicoEditorApp::SpawnEmptyActor()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.SpawnActor(false));
}

void FPicoEditorApp::DrawActorClassPicker()
{
    if (bOpenActorClassPicker)
    {
        ImGui::OpenPopup("Add Actor Class");
        bOpenActorClassPicker = false;
    }
    if (!ImGui::BeginPopupModal("Add Actor Class", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputTextWithHint("##ActorClassFilter", "Search Actor classes",
        ActorClassFilter.data(), ActorClassFilter.size());
    std::string Filter(ActorClassFilter.data());
    std::transform(Filter.begin(), Filter.end(), Filter.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    ImGui::BeginChild("ActorClassList", ImVec2(420.0f, 300.0f), true);
    auto Classes = FClassRegistry::GetClasses();
    std::sort(Classes.begin(), Classes.end(), [](const PClass* Left, const PClass* Right)
        { return Left->GetName().ToString() < Right->GetName().ToString(); });
    for (const PClass* Class : Classes)
    {
        if (Class == nullptr || !Class->CanConstruct() || !Class->IsChildOf(PActor::StaticClass())) continue;
        const std::string Name = Class->GetName().ToString();
        std::string Lower = Name;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        if (!Filter.empty() && Lower.find(Filter) == std::string::npos) continue;
        if (ImGui::Selectable(Name.c_str()))
        {
            FinishInteractiveEdit();
            ApplyCommandResult(CommandService.SpawnActor(Class));
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void FPicoEditorApp::DrawPlayableCharacterCreator()
{
    if (bOpenPlayableCharacterCreator)
    {
        if (PlayablePawnClassSetting[0] == '\0')
        {
            for (const PClass* Class : FClassRegistry::GetClasses())
            {
                if (Class != nullptr && Class->CanConstruct()
                    && Class->IsChildOf(PPawn::StaticClass()))
                {
                    const std::string Name = Class->GetName().ToString();
                    if (Name == "PSandboxPawn" || PlayablePawnClassSetting[0] == '\0')
                        std::snprintf(PlayablePawnClassSetting.data(),
                            PlayablePawnClassSetting.size(), "%s", Name.c_str());
                    if (Name == "PSandboxPawn") break;
                }
            }
        }
        ImGui::OpenPopup("Create Playable Character");
        bOpenPlayableCharacterCreator = false;
    }
    if (!ImGui::BeginPopupModal(
            "Create Playable Character", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SetNextItemWidth(520.0f);
    if (ImGui::BeginCombo("Pawn Class", PlayablePawnClassSetting.data()))
    {
        auto Classes = FClassRegistry::GetClasses();
        std::sort(Classes.begin(), Classes.end(), [](const PClass* Left, const PClass* Right)
            { return Left->GetName().ToString() < Right->GetName().ToString(); });
        for (const PClass* Class : Classes)
        {
            if (Class == nullptr || !Class->CanConstruct()
                || !Class->IsChildOf(PPawn::StaticClass())) continue;
            const std::string Name = Class->GetName().ToString();
            if (ImGui::Selectable(Name.c_str(), Name == PlayablePawnClassSetting.data()))
                std::snprintf(PlayablePawnClassSetting.data(),
                    PlayablePawnClassSetting.size(), "%s", Name.c_str());
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(520.0f);
    const char* ProfilePreview = PlayableCharacterProfileSetting[0] != '\0'
        ? PlayableCharacterProfileSetting.data() : "None";
    if (ImGui::BeginCombo("Character Profile", ProfilePreview))
    {
        if (ImGui::Selectable("None", PlayableCharacterProfileSetting[0] == '\0'))
            PlayableCharacterProfileSetting[0] = '\0';
        if (EngineLoop != nullptr)
        {
            for (const FAssetRecord& Record : EngineLoop->GetAssetRegistry().GetAssets())
            {
                if (Record.Type != EAssetType::CharacterProfile) continue;
                const std::string Path(Record.AssetPath.ToString());
                if (ImGui::Selectable(Path.c_str(), Path == PlayableCharacterProfileSetting.data()))
                    std::snprintf(PlayableCharacterProfileSetting.data(),
                        PlayableCharacterProfileSetting.size(), "%s", Path.c_str());
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("The created Pawn is saved in the map and auto-possessed by Player 0.");
    ImGui::Separator();
    if (ImGui::Button("Create"))
    {
        const PClass* SelectedClass = nullptr;
        for (const PClass* Class : FClassRegistry::GetClasses())
        {
            if (Class != nullptr
                && Class->GetName().ToString() == PlayablePawnClassSetting.data())
            {
                SelectedClass = Class;
                break;
            }
        }
        FAssetPath Profile;
        if (PlayableCharacterProfileSetting[0] != '\0')
            FAssetPath::TryParse(PlayableCharacterProfileSetting.data(), Profile);
        FinishInteractiveEdit();
        FEditorCommandResult Result =
            CommandService.SpawnPlayableCharacter(SelectedClass, Profile);
        const bool bSucceeded = Result.bSucceeded;
        ApplyCommandResult(std::move(Result));
        if (bSucceeded) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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

void FPicoEditorApp::AddScriptComponentToSelection()
{
    FinishInteractiveEdit();
    ApplyCommandResult(CommandService.AddScriptComponent());
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
    case DocumentActionOpenProject: PerformOpenProject(); break;
    case DocumentActionExit: bShouldClose = true; break;
    default: break;
    }
}

void FPicoEditorApp::OpenProject(bool bSelectFolder)
{
    const std::optional<std::filesystem::path> DialogSelection = bSelectFolder
        ? OpenProjectFolderDialog()
        : OpenProjectFileDialog();
    if (!DialogSelection.has_value())
    {
        return;
    }
    const FEditorProjectResolution Resolution =
        ResolveEditorProjectPath(*DialogSelection);
    if (!Resolution.IsResolved())
    {
        SetStatus(Resolution.Message, true);
        return;
    }
    if (Resolution.ProjectFile == FPaths::GetProjectFile())
    {
        SetStatus("The selected project is already open");
        return;
    }
    PendingProjectFile = Resolution.ProjectFile;
    RequestDocumentAction(DocumentActionOpenProject);
}

void FPicoEditorApp::PerformOpenProject()
{
    if (PendingProjectFile.empty())
    {
        return;
    }
    SaveEditorSession(true);
    StopGame(false);
    std::string Error;
    FProcessHandle NewEditor = FPlatformProcess::CreateProcess(
        FPaths::GetExecutablePath(),
        {PendingProjectFile.string()},
        FPaths::GetEngineRootDir(),
        &Error);
    if (!NewEditor.IsValid())
    {
        SetStatus("Could not open project: " + Error, true);
        PendingProjectFile.clear();
        return;
    }
    PendingProjectFile.clear();
    bShouldClose = true;
}

void FPicoEditorApp::SaveEditorSession(bool bForce)
{
    FEditorSessionState Session;
    if (WorldDocument.HasAssetPath())
    {
        Session.LastWorld = WorldDocument.GetAssetPath();
    }
    if (ActorBlueprintEditor.IsOpen())
    {
        Session.OpenActorBlueprint = ActorBlueprintEditor.GetOpenedAsset();
    }
    if (SkeletalAssetEditor.IsOpen())
    {
        Session.OpenSkeletalAsset = SkeletalAssetEditor.GetOpenedAsset();
    }
    Session.bAgentChatOpen = bAgentChatOpen;
    Session.bExternalAgentsOpen = bExternalAgentsOpen;
    const std::string Fingerprint =
        std::string(Session.LastWorld.ToString()) + "|"
        + std::string(Session.OpenActorBlueprint.ToString()) + "|"
        + std::string(Session.OpenSkeletalAsset.ToString()) + "|"
        + (Session.bAgentChatOpen ? "1" : "0") + "|"
        + (Session.bExternalAgentsOpen ? "1" : "0");
    if (!bForce && Fingerprint == SavedSessionFingerprint)
    {
        return;
    }
    std::filesystem::path SessionPath;
    if (FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Saved,
            "Editor/EditorSession.ini",
            SessionPath)
        && Session.Save(SessionPath))
    {
        SavedSessionFingerprint = Fingerprint;
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
