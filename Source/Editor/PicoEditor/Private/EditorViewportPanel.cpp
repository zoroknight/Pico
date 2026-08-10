#include "EditorViewportPanel.h"
#include "EditorTransformGizmo.h"

#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorToolState.h"
#include "Pico/Editor/EditorTransformService.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Pico
{
namespace
{
std::string MakeTransformDescription(
    EEditorTransformMode Mode,
    std::size_t TargetCount)
{
    const char* Verb = "Transform";
    switch (Mode)
    {
    case EEditorTransformMode::Translate:
        Verb = "Move";
        break;
    case EEditorTransformMode::Rotate:
        Verb = "Rotate";
        break;
    case EEditorTransformMode::Scale:
        Verb = "Scale";
        break;
    case EEditorTransformMode::Select:
        break;
    }
    return std::string(Verb) + " " + std::to_string(TargetCount) + " Object(s)";
}
}

FEditorViewportPanel::FEditorViewportPanel(
    FSceneViewportRenderer* InRenderer,
    GLFWwindow* InWindow)
    : Renderer(InRenderer)
    , Window(InWindow)
{
}

FEditorViewportPanel::~FEditorViewportPanel()
{
    EndCameraCapture();
}

void FEditorViewportPanel::Draw(
    PWorld* World,
    FAssetRegistry& AssetRegistry,
    FAssetManager& AssetManager,
    const FEditorSelection& Selection,
    const FEditorToolState& ToolState,
    FEditorTransformService& TransformService,
    float Width,
    float Height,
    bool bUseSceneCamera,
    const FSelectObject& SelectObject,
    const FSetStatus& SetStatus,
    const FBeginTransaction& BeginTransaction,
    const FFinishTransaction& FinishTransaction)
{
    ActiveTransformService = &TransformService;
    if (Renderer == nullptr || !Renderer->IsInitialized())
    {
        ImGui::TextDisabled("Viewport unavailable");
        return;
    }

    ImVec2 Available = ImGui::GetContentRegionAvail();
    Available.x = std::max(Width, 64.0f);
    Available.y = std::max(Height, 64.0f);
    const ImGuiIO& IO = ImGui::GetIO();
    const uint32 RenderWidth = static_cast<uint32>(std::clamp(
        Available.x * IO.DisplayFramebufferScale.x, 64.0f, 4096.0f));
    const uint32 RenderHeight = static_cast<uint32>(std::clamp(
        Available.y * IO.DisplayFramebufferScale.y, 64.0f, 4096.0f));

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
    View.NearPlane = CameraNearPlane;
    View.FarPlane = CameraFarPlane;
    if (bUseSceneCamera)
    {
        TryBuildActiveCameraView(World, View);
    }
    if (!Renderer->Resize(RenderWidth, RenderHeight)
        || !Renderer->Render(
            World,
            AssetRegistry,
            AssetManager,
            View,
            Selection.GetHandles()))
    {
        ImGui::TextDisabled("Viewport render failed");
        return;
    }

    ImGui::Image(
        reinterpret_cast<ImTextureID>(
            static_cast<std::uintptr_t>(Renderer->GetColorTexture())),
        Available,
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));

    const ImVec2 ItemMin = ImGui::GetItemRectMin();
    const ImVec2 ItemMax = ImGui::GetItemRectMax();
    const float ItemWidth = std::max(ItemMax.x - ItemMin.x, 1.0f);
    const float ItemHeight = std::max(ItemMax.y - ItemMin.y, 1.0f);
    const bool bHovered = ImGui::IsItemHovered();

    PObject* PrimarySelection = Selection.Resolve();
    if (PrimarySelection != nullptr
        && PrimarySelection->IsA(PPlayerStart::StaticClass()))
    {
        const auto* PlayerStart = static_cast<const PPlayerStart*>(PrimarySelection);
        const std::string Label = "Player Start "
            + std::to_string(PlayerStart->GetPlayerStartId());
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ItemMin.x + 12.0f, ItemMin.y + 12.0f),
            IM_COL32(70, 245, 145, 255),
            Label.c_str());
    }

    FEditorTransformGizmoResult GizmoResult;
    FTransform GizmoTransform;
    if (TransformService.GetGizmoTransform(Selection, GizmoTransform))
    {
        FEditorTransformGizmo Gizmo;
        GizmoResult = Gizmo.Draw(
            View,
            static_cast<float>(RenderWidth) / static_cast<float>(RenderHeight),
            ItemMin.x,
            ItemMin.y,
            ItemWidth,
            ItemHeight,
            ToolState,
            GizmoTransform);
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        bIgnoreGizmoUntilRelease = false;
    }

    const bool bWasTransforming = TransformService.IsManipulating();
    if (GizmoResult.bIsUsing && !bWasTransforming && !bIgnoreGizmoUntilRelease)
    {
        if (!TransformService.BeginManipulation(
                Selection,
                ToolState.TransformMode,
                ToolState.CoordinateSpace)
            || !BeginTransaction(MakeTransformDescription(
                ToolState.TransformMode,
                TransformService.GetTargetCount())))
        {
            TransformService.CancelManipulation();
            bIgnoreGizmoUntilRelease = true;
            SetStatus("Could not begin transform transaction");
        }
        else
        {
            bTransformChanged = false;
        }
    }

    if (GizmoResult.bChanged
        && TransformService.IsManipulating()
        && !bIgnoreGizmoUntilRelease)
    {
        if (TransformService.ApplyGizmoTransform(GizmoResult.Transform))
        {
            bTransformChanged = true;
        }
        else
        {
            TransformService.CancelManipulation();
            FinishTransaction(false);
            bIgnoreGizmoUntilRelease = true;
            bTransformChanged = false;
            SetStatus("Could not apply transform; changes were rolled back");
        }
    }

    if (bWasTransforming
        && !GizmoResult.bIsUsing
        && TransformService.IsManipulating())
    {
        const bool bCommit = bTransformChanged || TransformService.HasChanged();
        TransformService.EndManipulation();
        FinishTransaction(bCommit);
        SetStatus(bCommit ? "Transform committed" : "Transform unchanged");
        bTransformChanged = false;
    }

    const bool bGizmoConsumesMouse = bIgnoreGizmoUntilRelease
        || GizmoResult.bIsOver
        || GizmoResult.bIsUsing
        || TransformService.IsManipulating();
    if (bHovered
        && !bGizmoConsumesMouse
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 Mouse = ImGui::GetMousePos();
        const float LocalX = std::clamp((Mouse.x - ItemMin.x) / ItemWidth, 0.0f, 1.0f);
        const float LocalY = std::clamp((Mouse.y - ItemMin.y) / ItemHeight, 0.0f, 1.0f);
        const uint32 PixelX = std::min(
            static_cast<uint32>(LocalX * static_cast<float>(RenderWidth)),
            RenderWidth - 1);
        const uint32 PixelY = std::min(
            static_cast<uint32>((1.0f - LocalY) * static_cast<float>(RenderHeight)),
            RenderHeight - 1);
        PObject* PickedObject = ResolveObject(Renderer->Pick(PixelX, PixelY));
        if (PickedObject != nullptr && PickedObject->IsA(PActorComponent::StaticClass()))
        {
            PActor* Owner = static_cast<PActorComponent*>(PickedObject)->GetOwner();
            PickedObject = Owner != nullptr ? Owner : PickedObject;
        }
        const EEditorSelectionOperation Operation = IO.KeyCtrl
            ? EEditorSelectionOperation::Toggle
            : (IO.KeyShift
                ? EEditorSelectionOperation::Add
                : EEditorSelectionOperation::Replace);
        SelectObject(PickedObject, Operation, {});
        if (PickedObject != nullptr)
        {
            SetStatus(
                (Operation == EEditorSelectionOperation::Replace
                    ? "Selected " : "Updated selection with ")
                + PickedObject->GetPathName());
        }
        else if (Operation == EEditorSelectionOperation::Replace)
        {
            SetStatus("Cleared viewport selection");
        }
    }

    if (!bUseSceneCamera
        && bHovered
        && !bGizmoConsumesMouse
        && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        BeginCameraCapture();
    }
    if (bCameraCaptured
        && (bUseSceneCamera
            || Window == nullptr
            || glfwGetWindowAttrib(Window, GLFW_FOCUSED) == GLFW_FALSE
            || glfwGetMouseButton(Window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS))
    {
        EndCameraCapture();
    }
    if (!bCameraCaptured)
    {
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    double CursorX = LastCursorX;
    double CursorY = LastCursorY;
    glfwGetCursorPos(Window, &CursorX, &CursorY);
    const float DeltaX = std::clamp(static_cast<float>(CursorX - LastCursorX), -100.0f, 100.0f);
    const float DeltaY = std::clamp(static_cast<float>(CursorY - LastCursorY), -100.0f, 100.0f);
    LastCursorX = CursorX;
    LastCursorY = CursorY;
    CameraYawDegrees -= DeltaX * 0.08f;
    CameraPitchDegrees = std::clamp(CameraPitchDegrees - DeltaY * 0.08f, -89.0f, 89.0f);
    if (IO.MouseWheel != 0.0f)
    {
        CameraMoveSpeed = std::clamp(
            CameraMoveSpeed * std::pow(1.25f, IO.MouseWheel), 0.01f, 10000.0f);
        SetStatus("Camera speed " + std::to_string(static_cast<int>(CameraMoveSpeed)));
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
        const FVector3 Right =
            FVector3::Cross(UpdatedForward, FVector3::UpVector).GetSafeNormal();
        FVector3 Movement = FVector3::ZeroVector;
        if (glfwGetKey(Window, GLFW_KEY_W) == GLFW_PRESS) Movement += UpdatedForward;
        if (glfwGetKey(Window, GLFW_KEY_S) == GLFW_PRESS) Movement -= UpdatedForward;
        if (glfwGetKey(Window, GLFW_KEY_D) == GLFW_PRESS) Movement += Right;
        if (glfwGetKey(Window, GLFW_KEY_A) == GLFW_PRESS) Movement -= Right;
        if (glfwGetKey(Window, GLFW_KEY_E) == GLFW_PRESS) Movement += FVector3::UpVector;
        if (glfwGetKey(Window, GLFW_KEY_Q) == GLFW_PRESS) Movement -= FVector3::UpVector;
        if (Movement.Normalize())
        {
            const bool bShift = glfwGetKey(Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                || glfwGetKey(Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            CameraPosition += Movement * CameraMoveSpeed * (bShift ? 4.0f : 1.0f)
                * std::clamp(IO.DeltaTime, 0.0f, 0.1f);
        }
    }
}

bool FEditorViewportPanel::IsCameraCaptured() const
{
    return bCameraCaptured;
}

bool FEditorViewportPanel::IsTransformActive() const
{
    return ActiveTransformService != nullptr
        && ActiveTransformService->IsManipulating();
}

bool FEditorViewportPanel::CancelActiveTransform()
{
    if (!IsTransformActive())
    {
        return false;
    }
    ActiveTransformService->CancelManipulation();
    bIgnoreGizmoUntilRelease = true;
    bTransformChanged = false;
    return true;
}

bool FEditorViewportPanel::FocusSelection(
    const FAssetRegistry& AssetRegistry,
    FAssetManager& AssetManager,
    const FEditorSelection& Selection)
{
    FVector3 BoundsMin(std::numeric_limits<float>::max());
    FVector3 BoundsMax(std::numeric_limits<float>::lowest());
    bool bHasBounds = false;
    const auto AddPoint = [&BoundsMin, &BoundsMax, &bHasBounds](const FVector3& Point)
    {
        BoundsMin.X = std::min(BoundsMin.X, Point.X);
        BoundsMin.Y = std::min(BoundsMin.Y, Point.Y);
        BoundsMin.Z = std::min(BoundsMin.Z, Point.Z);
        BoundsMax.X = std::max(BoundsMax.X, Point.X);
        BoundsMax.Y = std::max(BoundsMax.Y, Point.Y);
        BoundsMax.Z = std::max(BoundsMax.Z, Point.Z);
        bHasBounds = true;
    };
    const auto AddTransformedBounds = [&AddPoint](
        const FVector3& LocalMin,
        const FVector3& LocalMax,
        const FTransform& Transform)
    {
        for (int X = 0; X < 2; ++X)
        {
            for (int Y = 0; Y < 2; ++Y)
            {
                for (int Z = 0; Z < 2; ++Z)
                {
                    AddPoint(Transform.TransformPosition(FVector3(
                        X == 0 ? LocalMin.X : LocalMax.X,
                        Y == 0 ? LocalMin.Y : LocalMax.Y,
                        Z == 0 ? LocalMin.Z : LocalMax.Z)));
                }
            }
        }
    };
    const auto AddComponent = [&](PSceneComponent* Component)
    {
        if (Component == nullptr)
        {
            return;
        }
        if (Component->IsA(PCubeComponent::StaticClass()))
        {
            const FVector3 Extent = static_cast<PCubeComponent*>(Component)->GetExtent();
            AddTransformedBounds(-Extent, Extent, Component->GetWorldTransform());
            return;
        }
        if (Component->IsA(PStaticMeshComponent::StaticClass()))
        {
            PStaticMeshComponent* StaticMesh = static_cast<PStaticMeshComponent*>(Component);
            const std::shared_ptr<const FStaticMeshData> Mesh = AssetManager.LoadStaticMesh(
                StaticMesh->GetStaticMeshAsset(), AssetRegistry);
            if (Mesh != nullptr)
            {
                AddTransformedBounds(
                    Mesh->Bounds.Min,
                    Mesh->Bounds.Max,
                    Component->GetWorldTransform());
                return;
            }
        }
        AddPoint(Component->GetWorldTransform().Translation);
    };

    for (PObject* Object : Selection.ResolveAll())
    {
        if (Object->IsA(PActor::StaticClass()))
        {
            PActor* Actor = static_cast<PActor*>(Object);
            bool bAddedPrimitive = false;
            for (PActorComponent* Component : Actor->GetComponents())
            {
                if (Component != nullptr && Component->IsA(PPrimitiveComponent::StaticClass()))
                {
                    AddComponent(static_cast<PSceneComponent*>(Component));
                    bAddedPrimitive = true;
                }
            }
            if (!bAddedPrimitive)
            {
                AddPoint(Actor->GetActorLocation());
            }
        }
        else if (Object->IsA(PSceneComponent::StaticClass()))
        {
            AddComponent(static_cast<PSceneComponent*>(Object));
        }
    }
    if (!bHasBounds)
    {
        return false;
    }

    const FVector3 Center = (BoundsMin + BoundsMax) * 0.5f;
    const FVector3 Extent = (BoundsMax - BoundsMin) * 0.5f;
    float Radius = std::sqrt(FVector3::Dot(Extent, Extent));
    Radius = std::max(Radius, 0.01f);
    const float YawRadians = DegreesToRadians(CameraYawDegrees);
    const float PitchRadians = DegreesToRadians(CameraPitchDegrees);
    const float CosPitch = std::cos(PitchRadians);
    const FVector3 Forward(
        CosPitch * std::cos(YawRadians),
        CosPitch * std::sin(YawRadians),
        std::sin(PitchRadians));
    const float Distance = std::max(
        Radius / std::tan(DegreesToRadians(25.0f)) * 1.25f,
        Radius * 2.0f);
    CameraPosition = Center - Forward * Distance;
    CameraNearPlane = std::clamp(Radius * 0.02f, 0.0001f, 1.0f);
    CameraFarPlane = std::max(10000.0f, Distance + Radius * 8.0f);
    CameraMoveSpeed = std::clamp(Radius * 4.0f, 0.01f, 10000.0f);
    return true;
}

void FEditorViewportPanel::InvalidateStaticMesh(const FAssetPath& AssetPath)
{
    if (Renderer != nullptr && Renderer->IsInitialized())
    {
        Renderer->InvalidateStaticMesh(AssetPath);
    }
}

void FEditorViewportPanel::BeginCameraCapture()
{
    if (Window == nullptr || bCameraCaptured)
    {
        return;
    }
    bCameraCaptured = true;
    glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    glfwGetCursorPos(Window, &LastCursorX, &LastCursorY);
}

void FEditorViewportPanel::EndCameraCapture()
{
    if (Window == nullptr || !bCameraCaptured)
    {
        return;
    }
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    bCameraCaptured = false;
}
}
