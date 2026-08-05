#pragma once

namespace Pico
{
enum class EEditorTransformMode
{
    Select,
    Translate,
    Rotate,
    Scale
};

enum class EEditorCoordinateSpace
{
    World,
    Local
};

struct FEditorToolState
{
    EEditorTransformMode TransformMode = EEditorTransformMode::Select;
    EEditorCoordinateSpace CoordinateSpace = EEditorCoordinateSpace::World;
    bool bSnapEnabled = false;
    float TranslationSnap = 10.0f;
    float RotationSnapDegrees = 10.0f;
    float ScaleSnap = 0.1f;
};
}
