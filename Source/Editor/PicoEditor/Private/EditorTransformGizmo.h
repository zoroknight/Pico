#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Editor/EditorToolState.h"

namespace Pico
{
struct FSceneView;

struct FEditorTransformGizmoResult
{
    FTransform Transform;
    bool bChanged = false;
    bool bIsOver = false;
    bool bIsUsing = false;
};

class FEditorTransformGizmo
{
public:
    FEditorTransformGizmoResult Draw(
        const FSceneView& View,
        float AspectRatio,
        float X,
        float Y,
        float Width,
        float Height,
        const FEditorToolState& ToolState,
        const FTransform& Transform);
};
}
