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
bool DrawVector3Control(const char* Label, FVector3& Value, float Speed = 0.1f)
{
    float Components[] = { Value.X, Value.Y, Value.Z };
    if (!ImGui::DragFloat3(Label, Components, Speed))
    {
        return false;
    }

    Value = FVector3(Components[0], Components[1], Components[2]);
    return true;
}

bool DrawRotatorControl(const char* Label, FRotator& Value)
{
    float Components[] = { Value.Pitch, Value.Yaw, Value.Roll };
    if (!ImGui::DragFloat3(Label, Components, 0.25f))
    {
        return false;
    }

    Value = FRotator(Components[0], Components[1], Components[2]).GetNormalized();
    return true;
}

bool DrawTransformControl(const char* Id, FTransform& Value)
{
    FRotator Rotation = Value.Rotation.Rotator();
    bool bChanged = false;

    ImGui::PushID(Id);
    if (ImGui::BeginTable(
            "TransformComponents",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Values", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Location");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        bChanged |= DrawVector3Control("##Location", Value.Translation);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        bChanged |= DrawRotatorControl("##Rotation", Rotation);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scale");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        bChanged |= DrawVector3Control("##Scale", Value.Scale, 0.01f);
        ImGui::EndTable();
    }
    ImGui::PopID();

    if (bChanged)
    {
        Value.Rotation = Rotation.Quaternion();
    }
    return bChanged;
}

void PushObjectId(const PObject* Object)
{
    const FObjectHandle Handle = Object->GetHandle();
    ImGui::PushID(static_cast<int>(Handle.Index));
    ImGui::PushID(static_cast<int>(Handle.Serial));
}

void PopObjectId()
{
    ImGui::PopID();
    ImGui::PopID();
}

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

bool IsValidObjectName(std::string_view Name)
{
    if (Name.empty()
        || !(std::isalpha(static_cast<unsigned char>(Name.front()))
            || Name.front() == '_'))
    {
        return false;
    }

    return std::all_of(
        Name.begin() + 1,
        Name.end(),
        [](char Character)
        {
            const unsigned char Value = static_cast<unsigned char>(Character);
            return std::isalnum(Value) || Character == '_';
        });
}

bool ContainsComponent(
    PSceneComponent* Component,
    FObjectHandle Handle)
{
    if (Component == nullptr || !Handle.IsValid())
    {
        return false;
    }
    if (Component->GetHandle() == Handle)
    {
        return true;
    }
    for (PSceneComponent* Child : Component->GetAttachChildren())
    {
        if (ContainsComponent(Child, Handle))
        {
            return true;
        }
    }
    return false;
}
}

FPicoEditorApp::FPicoEditorApp(
    FEngineLoop* InEngineLoop,
    FSceneViewportRenderer* InViewportRenderer,
    GLFWwindow* InWindow)
    : EngineLoop(InEngineLoop)
    , ViewportRenderer(InViewportRenderer)
    , Window(InWindow)
{
    PWorld* World = GetWorld();
    Select(World);
    SetStatus(
        World != nullptr ? "Editor world is ready" : "No active editor World",
        World == nullptr);
}

FPicoEditorApp::~FPicoEditorApp()
{
    EndViewportCameraCapture();
}

void FPicoEditorApp::Draw()
{
    HandleShortcuts();

    if (SelectedObjectHandle.IsValid() && GetSelectedObject() == nullptr)
    {
        SelectedObjectHandle = {};
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
        DrawSceneOutliner();
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
        DrawDetails();
    }
    ImGui::End();

    ProcessDeferredActions();
}

void FPicoEditorApp::DrawViewport(float Width, float Height)
{
    if (ViewportRenderer == nullptr || !ViewportRenderer->IsInitialized())
    {
        ImGui::TextDisabled("Viewport unavailable");
        return;
    }

    ImVec2 Available = ImGui::GetContentRegionAvail();
    Available.x = std::max(Width, 64.0f);
    Available.y = std::max(Height, 64.0f);
    const ImGuiIO& IO = ImGui::GetIO();
    const uint32 RenderWidth = static_cast<uint32>(std::clamp(
        Available.x * IO.DisplayFramebufferScale.x,
        64.0f,
        4096.0f));
    const uint32 RenderHeight = static_cast<uint32>(std::clamp(
        Available.y * IO.DisplayFramebufferScale.y,
        64.0f,
        4096.0f));

    const float YawRadians = DegreesToRadians(CameraYawDegrees);
    const float PitchRadians = DegreesToRadians(CameraPitchDegrees);
    const float CosPitch = std::cos(PitchRadians);
    const FVector3 Forward(
        CosPitch * std::cos(YawRadians),
        CosPitch * std::sin(YawRadians),
        std::sin(PitchRadians));

    FSceneView View;
    View.Position = CameraPosition;
    View.Target = CameraPosition + Forward;
    if (!ViewportRenderer->Resize(RenderWidth, RenderHeight)
        || !ViewportRenderer->Render(
            GetWorld(),
            View,
            SelectedObjectHandle))
    {
        ImGui::TextDisabled("Viewport render failed");
        return;
    }

    ImGui::Image(
        reinterpret_cast<ImTextureID>(
            static_cast<std::uintptr_t>(ViewportRenderer->GetColorTexture())),
        Available,
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));

    const bool bViewportHovered = ImGui::IsItemHovered();
    if (bViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 ItemMin = ImGui::GetItemRectMin();
        const ImVec2 ItemMax = ImGui::GetItemRectMax();
        const ImVec2 MousePosition = ImGui::GetMousePos();
        const float ItemWidth = std::max(ItemMax.x - ItemMin.x, 1.0f);
        const float ItemHeight = std::max(ItemMax.y - ItemMin.y, 1.0f);
        const float LocalX = std::clamp(
            (MousePosition.x - ItemMin.x) / ItemWidth,
            0.0f,
            1.0f);
        const float LocalY = std::clamp(
            (MousePosition.y - ItemMin.y) / ItemHeight,
            0.0f,
            1.0f);
        const uint32 PixelX = std::min(
            static_cast<uint32>(LocalX * static_cast<float>(RenderWidth)),
            RenderWidth - 1);
        const uint32 PixelY = std::min(
            static_cast<uint32>((1.0f - LocalY) * static_cast<float>(RenderHeight)),
            RenderHeight - 1);
        PObject* PickedObject =
            ResolveObject(ViewportRenderer->Pick(PixelX, PixelY));
        if (PickedObject != nullptr
            && PickedObject->IsA(PActorComponent::StaticClass()))
        {
            PActor* Owner = static_cast<PActorComponent*>(PickedObject)->GetOwner();
            PickedObject = Owner != nullptr ? Owner : PickedObject;
        }
        Select(PickedObject);
        SetStatus(
            PickedObject != nullptr
                ? "Selected " + PickedObject->GetPathName()
                : "Cleared viewport selection");
    }

    if (bViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        BeginViewportCameraCapture();
    }
    if (bViewportCameraCaptured
        && (Window == nullptr
            || glfwGetWindowAttrib(Window, GLFW_FOCUSED) == GLFW_FALSE
            || glfwGetMouseButton(Window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS))
    {
        EndViewportCameraCapture();
    }
    if (!bViewportCameraCaptured)
    {
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    double CursorX = LastCameraCursorX;
    double CursorY = LastCameraCursorY;
    glfwGetCursorPos(Window, &CursorX, &CursorY);
    const float MouseDeltaX = std::clamp(
        static_cast<float>(CursorX - LastCameraCursorX),
        -100.0f,
        100.0f);
    const float MouseDeltaY = std::clamp(
        static_cast<float>(CursorY - LastCameraCursorY),
        -100.0f,
        100.0f);
    LastCameraCursorX = CursorX;
    LastCameraCursorY = CursorY;
    CameraYawDegrees -= MouseDeltaX * 0.08f;
    CameraPitchDegrees = std::clamp(
        CameraPitchDegrees - MouseDeltaY * 0.08f,
        -89.0f,
        89.0f);
    if (IO.MouseWheel != 0.0f)
    {
        CameraMoveSpeed = std::clamp(
            CameraMoveSpeed * std::pow(1.25f, IO.MouseWheel),
            25.0f,
            10000.0f);
        SetStatus(
            "Camera speed " + std::to_string(
                static_cast<int>(CameraMoveSpeed)));
    }

    if (!IO.WantTextInput && Window != nullptr)
    {
        const float UpdatedYaw = DegreesToRadians(CameraYawDegrees);
        const float UpdatedPitch = DegreesToRadians(CameraPitchDegrees);
        const float UpdatedCosPitch = std::cos(UpdatedPitch);
        const FVector3 UpdatedForward(
            UpdatedCosPitch * std::cos(UpdatedYaw),
            UpdatedCosPitch * std::sin(UpdatedYaw),
            std::sin(UpdatedPitch));
        FVector3 Right =
            FVector3::Cross(UpdatedForward, FVector3::UpVector).GetSafeNormal();
        FVector3 Movement = FVector3::ZeroVector;
        if (glfwGetKey(Window, GLFW_KEY_W) == GLFW_PRESS)
        {
            Movement += UpdatedForward;
        }
        if (glfwGetKey(Window, GLFW_KEY_S) == GLFW_PRESS)
        {
            Movement -= UpdatedForward;
        }
        if (glfwGetKey(Window, GLFW_KEY_D) == GLFW_PRESS)
        {
            Movement += Right;
        }
        if (glfwGetKey(Window, GLFW_KEY_A) == GLFW_PRESS)
        {
            Movement -= Right;
        }
        if (glfwGetKey(Window, GLFW_KEY_E) == GLFW_PRESS)
        {
            Movement += FVector3::UpVector;
        }
        if (glfwGetKey(Window, GLFW_KEY_Q) == GLFW_PRESS)
        {
            Movement -= FVector3::UpVector;
        }

        if (Movement.Normalize())
        {
            const bool bShiftDown =
                glfwGetKey(Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                || glfwGetKey(Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            const float SpeedMultiplier = bShiftDown ? 4.0f : 1.0f;
            const float DeltaTime = std::clamp(IO.DeltaTime, 0.0f, 0.1f);
            CameraPosition +=
                Movement * CameraMoveSpeed * SpeedMultiplier * DeltaTime;
        }
    }
}

void FPicoEditorApp::BeginViewportCameraCapture()
{
    if (Window == nullptr || bViewportCameraCaptured)
    {
        return;
    }

    bViewportCameraCaptured = true;
    glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    glfwGetCursorPos(Window, &LastCameraCursorX, &LastCameraCursorY);
}

void FPicoEditorApp::EndViewportCameraCapture()
{
    if (Window == nullptr || !bViewportCameraCaptured)
    {
        return;
    }

    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    bViewportCameraCaptured = false;
}

PWorld* FPicoEditorApp::GetWorld() const
{
    return EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
}

PObject* FPicoEditorApp::GetSelectedObject() const
{
    return ResolveObject(SelectedObjectHandle);
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

    if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
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

void FPicoEditorApp::DrawSceneOutliner()
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        ImGui::TextDisabled("No active world");
        return;
    }

    PushObjectId(World);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (SelectedObjectHandle == World->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(World->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(World);
    }
    if (ImGui::BeginPopupContextItem("WorldContext"))
    {
        Select(World);
        if (ImGui::BeginMenu("Add Actor"))
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
        ImGui::EndPopup();
    }
    if (bOpen)
    {
        for (PLevel* Level : World->GetLevels())
        {
            DrawLevelNode(Level);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FPicoEditorApp::DrawLevelNode(PLevel* Level)
{
    if (Level == nullptr)
    {
        return;
    }

    PushObjectId(Level);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (SelectedObjectHandle == Level->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpen = ImGui::TreeNodeEx(Level->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Level);
    }
    if (ImGui::BeginPopupContextItem("LevelContext"))
    {
        Select(Level);
        if (ImGui::BeginMenu("Add Actor"))
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
        ImGui::EndPopup();
    }
    if (bOpen)
    {
        for (PActor* Actor : Level->GetActors())
        {
            DrawActorNode(Actor);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FPicoEditorApp::DrawActorNode(PActor* Actor)
{
    if (Actor == nullptr)
    {
        return;
    }

    const std::vector<PActorComponent*> Components = Actor->GetComponents();
    PushObjectId(Actor);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Components.empty())
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (SelectedObjectHandle == Actor->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    PObject* SelectedObject = GetSelectedObject();
    if (SelectedObject != nullptr
        && SelectedObject->IsA(PActorComponent::StaticClass())
        && static_cast<PActorComponent*>(SelectedObject)->GetOwner() == Actor)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    const bool bOpen = ImGui::TreeNodeEx(Actor->GetName().ToString().c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Actor);
    }
    DrawActorContextMenu(Actor);
    if (bOpen && !Components.empty())
    {
        PSceneComponent* RootComponent = Actor->GetRootComponent();
        if (RootComponent != nullptr)
        {
            DrawComponentNode(RootComponent, Actor);
        }

        for (PActorComponent* Component : Components)
        {
            if (Component == RootComponent)
            {
                continue;
            }

            if (Component->IsA(PSceneComponent::StaticClass())
                && static_cast<PSceneComponent*>(Component)->GetAttachParent() != nullptr)
            {
                continue;
            }
            DrawComponentNode(Component, Actor);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FPicoEditorApp::DrawComponentNode(PActorComponent* Component, PActor* Owner)
{
    if (Component == nullptr)
    {
        return;
    }

    PSceneComponent* SceneComponent =
        Component->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Component)
        : nullptr;
    const std::vector<PSceneComponent*> Children =
        SceneComponent != nullptr
        ? SceneComponent->GetAttachChildren()
        : std::vector<PSceneComponent*> {};

    PushObjectId(Component);
    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Children.empty())
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (SelectedObjectHandle == Component->GetHandle())
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string Label = Component->GetName().ToString();
    if (Owner != nullptr && Owner->GetRootComponent() == Component)
    {
        Label += " [Root]";
    }

    if (SceneComponent != nullptr
        && SceneComponent->GetHandle() != SelectedObjectHandle
        && ContainsComponent(SceneComponent, SelectedObjectHandle))
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Select(Component);
    }
    DrawComponentContextMenu(Component);
    if (bOpen && !Children.empty())
    {
        for (PSceneComponent* Child : Children)
        {
            DrawComponentNode(Child, Owner);
        }
        ImGui::TreePop();
    }
    PopObjectId();
}

void FPicoEditorApp::DrawActorContextMenu(PActor* Actor)
{
    if (!ImGui::BeginPopupContextItem("ActorContext"))
    {
        return;
    }

    Select(Actor);
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
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2"))
    {
        BeginRename(Actor);
    }
    if (ImGui::MenuItem("Delete", "Delete"))
    {
        QueueDestroy(Actor);
    }
    ImGui::EndPopup();
}

void FPicoEditorApp::DrawComponentContextMenu(PActorComponent* Component)
{
    if (!ImGui::BeginPopupContextItem("ComponentContext"))
    {
        return;
    }

    Select(Component);
    PSceneComponent* SceneComponent =
        Component != nullptr && Component->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Component)
        : nullptr;
    ImGui::BeginDisabled(SceneComponent == nullptr);
    if (ImGui::BeginMenu("Add Child Component"))
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

    PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
    const bool bCanSetRoot =
        SceneComponent != nullptr
        && Owner != nullptr
        && Owner->GetRootComponent() != SceneComponent;
    ImGui::BeginDisabled(!bCanSetRoot);
    if (ImGui::MenuItem("Set As Root"))
    {
        SetSelectedComponentAsRoot();
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2"))
    {
        BeginRename(Component);
    }
    if (ImGui::MenuItem("Delete", "Delete"))
    {
        QueueDestroy(Component);
    }
    ImGui::EndPopup();
}

void FPicoEditorApp::DrawDetails()
{
    PObject* Object = GetSelectedObject();
    if (Object == nullptr)
    {
        ImGui::TextDisabled("No object selected");
        return;
    }

    DrawObjectIdentity(Object);

    if (Object->IsA(PActor::StaticClass()))
    {
        DrawActorDetails(static_cast<PActor*>(Object));
    }
    else if (Object->IsA(PSceneComponent::StaticClass()))
    {
        DrawSceneComponentDetails(static_cast<PSceneComponent*>(Object));
    }
    else if (Object->IsA(PLevel::StaticClass()))
    {
        PLevel* Level = static_cast<PLevel*>(Object);
        ImGui::Separator();
        ImGui::Text("Actors: %zu", Level->GetActors().size());
    }
    else if (Object->IsA(PWorld::StaticClass()))
    {
        PWorld* World = static_cast<PWorld*>(Object);
        ImGui::Separator();
        ImGui::Text("Levels: %zu", World->GetLevels().size());
        ImGui::Text("Tick: %llu", static_cast<unsigned long long>(World->GetTickCount()));
        ImGui::Text("Time: %.3f s", World->GetTimeSeconds());
    }
}

void FPicoEditorApp::DrawObjectIdentity(PObject* Object)
{
    const FObjectHandle Handle = Object->GetHandle();
    ImGui::Text("Name: %s", Object->GetName().ToString().c_str());
    ImGui::Text("Class: %s", Object->GetClass()->GetName().ToString().c_str());
    ImGui::Text("Path: %s", Object->GetPathName().c_str());
    ImGui::Text(
        "Outer: %s",
        Object->GetOuter() != nullptr ? Object->GetOuter()->GetPathName().c_str() : "None");
    ImGui::Text("Handle: {%u, %u}", Handle.Index, Handle.Serial);
}

void FPicoEditorApp::DrawActorDetails(PActor* Actor)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");

    PSceneComponent* RootComponent = Actor->GetRootComponent();
    if (RootComponent == nullptr)
    {
        ImGui::TextDisabled("No RootComponent");
        if (ImGui::Button("Add Scene Root"))
        {
            AddRootToSelectedActor();
        }
    }
    else
    {
        FTransform Transform = Actor->GetActorTransform();
        if (DrawTransformControl("ActorTransform", Transform))
        {
            if (Actor->SetActorTransform(Transform))
            {
                SetStatus("Changed " + Actor->GetPathName() + " transform");
            }
        }
        ImGui::TextDisabled("Provided by %s", RootComponent->GetName().ToString().c_str());
    }

    ImGui::Separator();
    ImGui::Text("Components: %zu", Actor->GetComponents().size());
    ImGui::Text("Begun Play: %s", Actor->HasBegunPlay() ? "true" : "false");
}

void FPicoEditorApp::DrawSceneComponentDetails(PSceneComponent* Component)
{
    PActor* Owner = Component->GetOwner();
    ImGui::Separator();
    ImGui::Text(
        "Owner: %s",
        Owner != nullptr ? Owner->GetPathName().c_str() : "None");
    ImGui::Text(
        "Role: %s",
        Owner != nullptr && Owner->GetRootComponent() == Component ? "RootComponent" : "SceneComponent");
    PSceneComponent* Parent = Component->GetAttachParent();
    ImGui::Text(
        "Attach Parent: %s",
        Parent != nullptr ? Parent->GetPathName().c_str() : "None");
    ImGui::Text("Attach Children: %zu", Component->GetAttachChildren().size());
    ImGui::Text("Registered: %s", Component->IsRegistered() ? "true" : "false");

    ImGui::Separator();
    ImGui::TextUnformatted("World Transform");
    FTransform WorldTransform = Component->GetWorldTransform();
    ImGui::BeginDisabled();
    DrawTransformControl("WorldTransform", WorldTransform);
    ImGui::EndDisabled();

    DrawReflectedProperties(Component);
}

void FPicoEditorApp::DrawReflectedProperties(PObject* Object)
{
    const std::vector<const PProperty*> Properties = GetAllProperties(Object->GetClass());
    if (Properties.empty())
    {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Reflected Properties");
    if (ImGui::BeginTable(
            "ReflectedProperties",
            2,
            ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_BordersInnerH
                | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        for (const PProperty* Property : Properties)
        {
            DrawPropertyEditor(Object, Property);
        }
        ImGui::EndTable();
    }
}

void FPicoEditorApp::DrawPropertyEditor(PObject* Object, const PProperty* Property)
{
    const std::string PropertyName = Property->GetName().ToString();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(PropertyName.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushID(Property);

    bool bChanged = false;
    switch (Property->GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        bChanged =
            Property->GetValue(Object, Value)
            && ImGui::InputInt("##Value", &Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        bChanged =
            Property->GetValue(Object, Value)
            && ImGui::DragFloat("##Value", &Value, 0.1f)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        bChanged =
            Property->GetValue(Object, Value)
            && ImGui::Checkbox("##Value", &Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        bChanged =
            Property->GetValue(Object, Value)
            && DrawVector3Control("##Value", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        bChanged =
            Property->GetValue(Object, Value)
            && DrawRotatorControl("##Value", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        bChanged =
            Property->GetValue(Object, Value)
            && DrawTransformControl("PropertyTransform", Value)
            && Property->SetValue(Object, Value);
        break;
    }
    }

    if (bChanged)
    {
        SetStatus("Changed " + Object->GetPathName() + "." + PropertyName);
    }
    ImGui::PopID();
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
    if (!PendingDestroyHandle.IsValid())
    {
        return;
    }

    PObject* Object = ResolveObject(PendingDestroyHandle);
    PendingDestroyHandle = {};
    if (Object == nullptr)
    {
        return;
    }

    Select(Object);
    DestroySelectedObject();
}

PActor* FPicoEditorApp::CreateActor(std::string Name)
{
    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        return nullptr;
    }

    return World->SpawnActor<PActor>(Name);
}

PActor* FPicoEditorApp::CreateCubeActor(std::string Name)
{
    PActor* Actor = CreateActor(std::move(Name));
    PWorld* World = GetWorld();
    if (Actor == nullptr || World == nullptr)
    {
        return nullptr;
    }

    PCubeComponent* Cube = Actor->CreateComponent<PCubeComponent>("CubeComponent");
    if (Cube == nullptr || !Actor->SetRootComponent(Cube))
    {
        World->DestroyActor(Actor);
        return nullptr;
    }
    return Actor;
}

PSceneComponent* FPicoEditorApp::AddSceneRoot(PActor* Actor)
{
    if (Actor == nullptr || Actor->GetRootComponent() != nullptr)
    {
        return nullptr;
    }

    PSceneComponent* Root =
        Actor->CreateComponent<PSceneComponent>("DefaultSceneRoot");
    if (Root == nullptr || !Actor->SetRootComponent(Root))
    {
        if (Root != nullptr)
        {
            Actor->DestroyComponent(Root);
        }
        return nullptr;
    }
    return Root;
}

PSceneComponent* FPicoEditorApp::AddComponent(
    PActor* Actor,
    PSceneComponent* AttachParent,
    bool bCubeComponent)
{
    if (Actor == nullptr
        || (AttachParent != nullptr && AttachParent->GetOwner() != Actor))
    {
        return nullptr;
    }

    unsigned int& NextNumber =
        bCubeComponent ? NextCubeComponentNumber : NextComponentNumber;
    const char* NamePrefix =
        bCubeComponent ? "CubeComponent_" : "SceneComponent_";
    PSceneComponent* Component = nullptr;
    do
    {
        const std::string Name = NamePrefix + std::to_string(NextNumber++);
        Component = bCubeComponent
            ? static_cast<PSceneComponent*>(
                Actor->CreateComponent<PCubeComponent>(Name))
            : Actor->CreateComponent<PSceneComponent>(Name);
    }
    while (Component == nullptr && NextNumber < 10000);

    if (Component == nullptr)
    {
        return nullptr;
    }

    PSceneComponent* Root = Actor->GetRootComponent();
    const bool bConnected =
        Root == nullptr
        ? Actor->SetRootComponent(Component)
        : Component->AttachToComponent(
            AttachParent != nullptr ? AttachParent : Root,
            EAttachmentTransformRule::KeepRelative);
    if (!bConnected)
    {
        Actor->DestroyComponent(Component);
        return nullptr;
    }
    return Component;
}

void FPicoEditorApp::SpawnEmptyActor()
{
    std::string Name;
    PActor* Actor = nullptr;
    do
    {
        Name = "Actor_" + std::to_string(NextActorNumber++);
        Actor = CreateActor(Name);
        if (Actor != nullptr && AddSceneRoot(Actor) == nullptr)
        {
            if (PWorld* World = Actor->GetWorld())
            {
                World->DestroyActor(Actor);
            }
            Actor = nullptr;
        }
    }
    while (Actor == nullptr && NextActorNumber < 10000);

    if (Actor == nullptr)
    {
        SetStatus("Failed to spawn an Actor", true);
        return;
    }

    Select(Actor);
    SetStatus("Spawned " + Actor->GetPathName());
}

void FPicoEditorApp::SpawnCubeActor()
{
    std::string Name;
    PActor* Actor = nullptr;
    do
    {
        Name = "Cube_" + std::to_string(NextCubeNumber++);
        Actor = CreateCubeActor(Name);
    }
    while (Actor == nullptr && NextCubeNumber < 10000);

    if (Actor == nullptr)
    {
        SetStatus("Failed to spawn a Cube", true);
        return;
    }

    Select(Actor);
    SetStatus("Spawned " + Actor->GetPathName());
}

void FPicoEditorApp::AddRootToSelectedActor()
{
    PObject* Object = GetSelectedObject();
    PActor* Actor =
        Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object)
        : nullptr;
    PSceneComponent* Root = AddSceneRoot(Actor);
    if (Root == nullptr)
    {
        SetStatus("Selected Actor could not create a scene root", true);
        return;
    }

    Select(Root);
    SetStatus("Added " + Root->GetPathName());
}

void FPicoEditorApp::AddComponentToSelection(bool bCubeComponent)
{
    PObject* Object = GetSelectedObject();
    PActor* Actor =
        Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object)
        : nullptr;
    PSceneComponent* Parent =
        Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object)
        : nullptr;
    if (Actor == nullptr && Parent != nullptr)
    {
        Actor = Parent->GetOwner();
    }
    if (Actor == nullptr)
    {
        SetStatus("Select an Actor or SceneComponent before adding a component", true);
        return;
    }

    PSceneComponent* Component = AddComponent(Actor, Parent, bCubeComponent);
    if (Component == nullptr)
    {
        SetStatus("Could not add the selected component type", true);
        return;
    }

    Select(Component);
    SetStatus("Added " + Component->GetPathName());
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
    PObject* Object = GetSelectedObject();
    PSceneComponent* Component =
        Object != nullptr && Object->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Object)
        : nullptr;
    PActor* Owner = Component != nullptr ? Component->GetOwner() : nullptr;
    if (Owner == nullptr || !Owner->SetRootComponent(Component))
    {
        SetStatus("Selected component could not become the root", true);
        return;
    }

    SetStatus("Set " + Component->GetPathName() + " as RootComponent");
}

void FPicoEditorApp::DestroySelectedObject()
{
    PObject* Object = GetSelectedObject();
    if (Object == nullptr)
    {
        SetStatus("No object selected", true);
        return;
    }

    const std::string Path = Object->GetPathName();
    FObjectHandle SelectionAfterDestroy;
    bool bDestroyed = false;
    if (Object->IsA(PActor::StaticClass()))
    {
        PActor* Actor = static_cast<PActor*>(Object);
        PWorld* World = Actor->GetWorld();
        SelectionAfterDestroy =
            World != nullptr ? World->GetHandle() : FObjectHandle {};
        bDestroyed = World != nullptr && World->DestroyActor(Actor);
    }
    else if (Object->IsA(PActorComponent::StaticClass()))
    {
        PActorComponent* Component = static_cast<PActorComponent*>(Object);
        PActor* Owner = Component->GetOwner();
        SelectionAfterDestroy =
            Owner != nullptr ? Owner->GetHandle() : FObjectHandle {};
        bDestroyed = Owner != nullptr && Owner->DestroyComponent(Component);
    }

    if (!bDestroyed)
    {
        SetStatus("Could not destroy " + Path, true);
        return;
    }

    Select(ResolveObject(SelectionAfterDestroy));
    SetStatus("Destroyed " + Path);
}

void FPicoEditorApp::QueueDestroy(PObject* Object)
{
    PendingDestroyHandle =
        Object != nullptr ? Object->GetHandle() : FObjectHandle {};
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
    PObject* Object = ResolveObject(RenameObjectHandle);
    const std::string NewName(RenameBuffer.data());
    if (Object == nullptr)
    {
        SetStatus("The object being renamed no longer exists", true);
        RenameObjectHandle = {};
        return false;
    }
    if (!IsValidObjectName(NewName))
    {
        SetStatus(
            "Names must start with a letter or underscore and contain only letters, numbers, or underscores",
            true);
        return false;
    }

    const std::string OldPath = Object->GetPathName();
    if (!RenameObject(Object, FName(NewName)))
    {
        SetStatus("The name is already used in this object scope", true);
        return false;
    }

    RenameObjectHandle = {};
    SetStatus("Renamed " + OldPath + " to " + Object->GetPathName());
    return true;
}

void FPicoEditorApp::SaveWorld()
{
    PWorld* World = GetWorld();
    std::filesystem::path WorldPath;
    if (World == nullptr || !GetDefaultWorldPath(WorldPath))
    {
        SetStatus("Cannot save without an active project World", true);
        return;
    }

    std::error_code FileError;
    std::filesystem::create_directories(WorldPath.parent_path(), FileError);
    if (FileError)
    {
        SetStatus("Could not create the project Maps directory", true);
        return;
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!SaveWorldToFile(WorldPath, *World, &Error))
    {
        SetStatus(
            "Could not save World: " + std::string(ToString(Error)),
            true);
        return;
    }

    SetStatus("Saved " + WorldPath.string());
}

void FPicoEditorApp::OpenWorld()
{
    std::filesystem::path WorldPath;
    if (EngineLoop == nullptr || !GetDefaultWorldPath(WorldPath))
    {
        SetStatus("Cannot open a World without an active project", true);
        return;
    }

    EWorldSerializationError Error = EWorldSerializationError::None;
    if (!EngineLoop->LoadWorld(WorldPath, &Error))
    {
        SetStatus(
            "Could not open World: " + std::string(ToString(Error)),
            true);
        return;
    }

    Select(GetWorld());
    SetStatus("Opened " + WorldPath.string());
}

bool FPicoEditorApp::GetDefaultWorldPath(std::filesystem::path& OutPath) const
{
    return FPaths::TryGetProjectWritePath(
        EProjectWriteRoot::Content,
        std::filesystem::path("Maps") / "EditorWorld.pworld",
        OutPath);
}

void FPicoEditorApp::Select(PObject* Object)
{
    SelectedObjectHandle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
}

void FPicoEditorApp::SetStatus(std::string Message, bool bIsError)
{
    Status = std::move(Message);
    bStatusIsError = bIsError;
}
}
