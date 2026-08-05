#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Editor/EditorToolState.h"
#include "Pico/Object/ObjectTypes.h"

#include <cstddef>
#include <vector>

namespace Pico
{
class FEditorSelection;

class FEditorTransformService
{
public:
    bool GetGizmoTransform(
        const FEditorSelection& Selection,
        FTransform& OutTransform) const;
    bool BeginManipulation(
        const FEditorSelection& Selection,
        EEditorTransformMode Mode,
        EEditorCoordinateSpace CoordinateSpace);
    bool ApplyGizmoTransform(const FTransform& GizmoTransform);
    void EndManipulation();
    bool CancelManipulation();

    bool IsManipulating() const;
    bool HasChanged() const;
    std::size_t GetTargetCount() const;
    const FTransform& GetCurrentGizmoTransform() const;

private:
    struct FTarget
    {
        FObjectHandle Handle;
        FTransform InitialWorldTransform;
    };

    static bool BuildTargets(
        const FEditorSelection& Selection,
        std::vector<FTarget>& OutTargets,
        std::size_t& OutPrimaryTargetIndex);
    static bool GetTargetTransform(FObjectHandle Handle, FTransform& OutTransform);
    static bool SetTargetTransform(FObjectHandle Handle, const FTransform& Transform);
    void Reset();

    std::vector<FTarget> Targets;
    FTransform InitialGizmoTransform;
    FTransform CurrentGizmoTransform;
    EEditorTransformMode Mode = EEditorTransformMode::Select;
    EEditorCoordinateSpace CoordinateSpace = EEditorCoordinateSpace::World;
    bool bManipulating = false;
    bool bChanged = false;
};
}
