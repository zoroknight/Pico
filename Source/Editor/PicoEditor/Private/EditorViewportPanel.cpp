#include "EditorViewportPanel.h"

#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Pico
{
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
    const FEditorSelection& Selection,
    float Width,
    float Height,
    const FSelectObject& SelectObject,
    const FSetStatus& SetStatus)
{
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
    if (!Renderer->Resize(RenderWidth, RenderHeight)
        || !Renderer->Render(World, View, Selection.GetHandle()))
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

    const bool bHovered = ImGui::IsItemHovered();
    if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 ItemMin = ImGui::GetItemRectMin();
        const ImVec2 ItemMax = ImGui::GetItemRectMax();
        const ImVec2 Mouse = ImGui::GetMousePos();
        const float ItemWidth = std::max(ItemMax.x - ItemMin.x, 1.0f);
        const float ItemHeight = std::max(ItemMax.y - ItemMin.y, 1.0f);
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
        SelectObject(PickedObject);
        SetStatus(PickedObject != nullptr
            ? "Selected " + PickedObject->GetPathName()
            : "Cleared viewport selection");
    }

    if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        BeginCameraCapture();
    }
    if (bCameraCaptured
        && (Window == nullptr
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
            CameraMoveSpeed * std::pow(1.25f, IO.MouseWheel), 25.0f, 10000.0f);
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
