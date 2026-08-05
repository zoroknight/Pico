#include "EditorTransformGizmo.h"

#include "Pico/Core/Math/Matrix4.h"
#include "Pico/Core/Math/Rotator.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <array>
#include <cstddef>

namespace Pico
{
namespace
{
std::array<float, 16> ToColumnMajor(const FMatrix4& Matrix)
{
    std::array<float, 16> Result {};
    for (std::size_t Row = 0; Row < 4; ++Row)
    {
        for (std::size_t Column = 0; Column < 4; ++Column)
        {
            Result[Column * 4 + Row] = Matrix[Row][Column];
        }
    }
    return Result;
}

FTransform FromColumnMajor(const float* Matrix)
{
    float Translation[3] {};
    float Rotation[3] {};
    float Scale[3] {};
    ImGuizmo::DecomposeMatrixToComponents(
        Matrix,
        Translation,
        Rotation,
        Scale);
    return FTransform(
        FRotator(Rotation[1], Rotation[2], Rotation[0]),
        FVector3(Translation[0], Translation[1], Translation[2]),
        FVector3(Scale[0], Scale[1], Scale[2]));
}

ImGuizmo::OPERATION ToOperation(EEditorTransformMode Mode)
{
    switch (Mode)
    {
    case EEditorTransformMode::Translate:
        return ImGuizmo::TRANSLATE;
    case EEditorTransformMode::Rotate:
        return ImGuizmo::ROTATE;
    case EEditorTransformMode::Scale:
        return ImGuizmo::SCALE;
    case EEditorTransformMode::Select:
        break;
    }
    return ImGuizmo::TRANSLATE;
}
}

FEditorTransformGizmoResult FEditorTransformGizmo::Draw(
    const FSceneView& View,
    float AspectRatio,
    float X,
    float Y,
    float Width,
    float Height,
    const FEditorToolState& ToolState,
    const FTransform& Transform)
{
    FEditorTransformGizmoResult Result;
    Result.Transform = Transform;
    if (ToolState.TransformMode == EEditorTransformMode::Select
        || Width <= 0.0f
        || Height <= 0.0f)
    {
        return Result;
    }

    std::array<float, 16> ViewMatrix =
        ToColumnMajor(BuildSceneViewMatrix(View));
    std::array<float, 16> ProjectionMatrix =
        ToColumnMajor(BuildSceneProjectionMatrix(View, AspectRatio));
    std::array<float, 16> TransformMatrix =
        ToColumnMajor(Transform.ToMatrix());

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(X, Y, Width, Height);
    ImGuizmo::PushID(0x5049434f);

    const ImGuizmo::OPERATION Operation = ToOperation(ToolState.TransformMode);
    const ImGuizmo::MODE GizmoMode =
        ToolState.TransformMode == EEditorTransformMode::Scale
            || ToolState.CoordinateSpace == EEditorCoordinateSpace::Local
        ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    std::array<float, 3> Snap {};
    const float* SnapValues = nullptr;
    if (ToolState.bSnapEnabled)
    {
        switch (ToolState.TransformMode)
        {
        case EEditorTransformMode::Translate:
            Snap.fill(ToolState.TranslationSnap);
            break;
        case EEditorTransformMode::Rotate:
            Snap.fill(ToolState.RotationSnapDegrees);
            break;
        case EEditorTransformMode::Scale:
            Snap.fill(ToolState.ScaleSnap);
            break;
        case EEditorTransformMode::Select:
            break;
        }
        SnapValues = Snap.data();
    }

    Result.bChanged = ImGuizmo::Manipulate(
        ViewMatrix.data(),
        ProjectionMatrix.data(),
        Operation,
        GizmoMode,
        TransformMatrix.data(),
        nullptr,
        SnapValues);
    Result.bIsOver = ImGuizmo::IsOver(Operation);
    Result.bIsUsing = ImGuizmo::IsUsing();
    ImGuizmo::PopID();
    if (Result.bChanged || Result.bIsUsing)
    {
        Result.Transform = FromColumnMajor(TransformMatrix.data());
    }
    return Result;
}
}
