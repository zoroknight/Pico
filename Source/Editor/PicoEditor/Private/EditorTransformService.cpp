#include "Pico/Editor/EditorTransformService.h"

#include "Pico/Editor/EditorSelection.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Pico
{
namespace
{
float SafeScaleRatio(float Current, float Initial)
{
    return std::abs(Initial) > SmallNumber ? Current / Initial : 1.0f;
}

float ClampScale(float Scale)
{
    constexpr float MinimumScale = 0.001f;
    if (std::abs(Scale) >= MinimumScale)
    {
        return Scale;
    }
    return Scale < 0.0f ? -MinimumScale : MinimumScale;
}
}

bool FEditorTransformService::GetGizmoTransform(
    const FEditorSelection& Selection,
    FTransform& OutTransform) const
{
    if (bManipulating)
    {
        OutTransform = CurrentGizmoTransform;
        return true;
    }

    std::vector<FTarget> SelectionTargets;
    std::size_t PrimaryIndex = 0;
    if (!BuildTargets(Selection, SelectionTargets, PrimaryIndex))
    {
        return false;
    }
    OutTransform = SelectionTargets[PrimaryIndex].InitialWorldTransform;
    return true;
}

bool FEditorTransformService::BeginManipulation(
    const FEditorSelection& Selection,
    EEditorTransformMode InMode,
    EEditorCoordinateSpace InCoordinateSpace)
{
    if (bManipulating || InMode == EEditorTransformMode::Select)
    {
        return false;
    }

    std::size_t PrimaryIndex = 0;
    if (!BuildTargets(Selection, Targets, PrimaryIndex))
    {
        Reset();
        return false;
    }

    Mode = InMode;
    CoordinateSpace = InCoordinateSpace;
    InitialGizmoTransform = Targets[PrimaryIndex].InitialWorldTransform;
    CurrentGizmoTransform = InitialGizmoTransform;
    bManipulating = true;
    bChanged = false;
    return true;
}

bool FEditorTransformService::ApplyGizmoTransform(
    const FTransform& GizmoTransform)
{
    if (!bManipulating)
    {
        return false;
    }
    for (const FTarget& Target : Targets)
    {
        FTransform CurrentTargetTransform;
        if (!GetTargetTransform(Target.Handle, CurrentTargetTransform))
        {
            return false;
        }
    }

    const FVector3 TranslationDelta =
        GizmoTransform.Translation - InitialGizmoTransform.Translation;
    const FQuat RotationDelta =
        (GizmoTransform.Rotation * InitialGizmoTransform.Rotation.Inverse())
            .GetNormalized();
    const FVector3 ScaleRatio(
        SafeScaleRatio(GizmoTransform.Scale.X, InitialGizmoTransform.Scale.X),
        SafeScaleRatio(GizmoTransform.Scale.Y, InitialGizmoTransform.Scale.Y),
        SafeScaleRatio(GizmoTransform.Scale.Z, InitialGizmoTransform.Scale.Z));
    const FQuat ScaleBasis = CoordinateSpace == EEditorCoordinateSpace::Local
        ? InitialGizmoTransform.Rotation : FQuat::Identity;

    for (const FTarget& Target : Targets)
    {
        FTransform Transform = Target.InitialWorldTransform;
        switch (Mode)
        {
        case EEditorTransformMode::Translate:
            Transform.Translation += TranslationDelta;
            break;
        case EEditorTransformMode::Rotate:
        {
            const FVector3 Offset =
                Target.InitialWorldTransform.Translation
                - InitialGizmoTransform.Translation;
            Transform.Translation = InitialGizmoTransform.Translation
                + RotationDelta.RotateVector(Offset);
            Transform.Rotation =
                (RotationDelta * Target.InitialWorldTransform.Rotation)
                    .GetNormalized();
            break;
        }
        case EEditorTransformMode::Scale:
        {
            const FVector3 Offset =
                Target.InitialWorldTransform.Translation
                - InitialGizmoTransform.Translation;
            const FVector3 LocalOffset = ScaleBasis.Inverse().RotateVector(Offset);
            Transform.Translation = InitialGizmoTransform.Translation
                + ScaleBasis.RotateVector(LocalOffset * ScaleRatio);
            Transform.Scale = FVector3(
                ClampScale(Target.InitialWorldTransform.Scale.X * ScaleRatio.X),
                ClampScale(Target.InitialWorldTransform.Scale.Y * ScaleRatio.Y),
                ClampScale(Target.InitialWorldTransform.Scale.Z * ScaleRatio.Z));
            break;
        }
        case EEditorTransformMode::Select:
            return false;
        }

        if (!SetTargetTransform(Target.Handle, Transform))
        {
            return false;
        }
    }

    CurrentGizmoTransform = GizmoTransform;
    bChanged = bChanged || !GizmoTransform.Equals(InitialGizmoTransform);
    return true;
}

void FEditorTransformService::EndManipulation()
{
    Reset();
}

bool FEditorTransformService::CancelManipulation()
{
    if (!bManipulating)
    {
        return false;
    }
    bool bRestored = true;
    for (const FTarget& Target : Targets)
    {
        bRestored = SetTargetTransform(
            Target.Handle,
            Target.InitialWorldTransform) && bRestored;
    }
    Reset();
    return bRestored;
}

bool FEditorTransformService::IsManipulating() const
{
    return bManipulating;
}

bool FEditorTransformService::HasChanged() const
{
    return bChanged;
}

std::size_t FEditorTransformService::GetTargetCount() const
{
    return Targets.size();
}

const FTransform& FEditorTransformService::GetCurrentGizmoTransform() const
{
    return CurrentGizmoTransform;
}

bool FEditorTransformService::BuildTargets(
    const FEditorSelection& Selection,
    std::vector<FTarget>& OutTargets,
    std::size_t& OutPrimaryTargetIndex)
{
    OutTargets.clear();
    OutPrimaryTargetIndex = 0;
    const FObjectHandle PrimaryHandle = Selection.GetHandle();
    std::size_t PrimaryIndex = std::numeric_limits<std::size_t>::max();

    for (PObject* Object : Selection.ResolveAll())
    {
        if (Object == nullptr)
        {
            continue;
        }

        FObjectHandle TargetHandle;
        FTransform Transform;
        if (Object->IsA(PActor::StaticClass()))
        {
            PActor* Actor = static_cast<PActor*>(Object);
            if (Actor->GetRootComponent() == nullptr)
            {
                continue;
            }
            TargetHandle = Actor->GetHandle();
            Transform = Actor->GetActorTransform();
        }
        else if (Object->IsA(PSceneComponent::StaticClass()))
        {
            PSceneComponent* Component = static_cast<PSceneComponent*>(Object);
            if (Selection.Contains(Component->GetOwner()))
            {
                continue;
            }

            bool bAncestorSelected = false;
            for (PSceneComponent* Parent = Component->GetAttachParent();
                 Parent != nullptr;
                 Parent = Parent->GetAttachParent())
            {
                if (Selection.Contains(Parent))
                {
                    bAncestorSelected = true;
                    break;
                }
            }
            if (bAncestorSelected)
            {
                continue;
            }
            TargetHandle = Component->GetHandle();
            Transform = Component->GetWorldTransform();
        }
        else
        {
            continue;
        }

        if (TargetHandle == PrimaryHandle)
        {
            PrimaryIndex = OutTargets.size();
        }
        OutTargets.push_back(FTarget { TargetHandle, Transform });
    }

    if (OutTargets.empty())
    {
        return false;
    }
    OutPrimaryTargetIndex = PrimaryIndex < OutTargets.size()
        ? PrimaryIndex : OutTargets.size() - 1;
    return true;
}

bool FEditorTransformService::GetTargetTransform(
    FObjectHandle Handle,
    FTransform& OutTransform)
{
    PObject* Object = ResolveObject(Handle);
    if (Object != nullptr && Object->IsA(PActor::StaticClass()))
    {
        PActor* Actor = static_cast<PActor*>(Object);
        if (Actor->GetRootComponent() != nullptr)
        {
            OutTransform = Actor->GetActorTransform();
            return true;
        }
    }
    if (Object != nullptr && Object->IsA(PSceneComponent::StaticClass()))
    {
        OutTransform = static_cast<PSceneComponent*>(Object)->GetWorldTransform();
        return true;
    }
    return false;
}

bool FEditorTransformService::SetTargetTransform(
    FObjectHandle Handle,
    const FTransform& Transform)
{
    PObject* Object = ResolveObject(Handle);
    if (Object != nullptr && Object->IsA(PActor::StaticClass()))
    {
        return static_cast<PActor*>(Object)->SetActorTransform(Transform);
    }
    if (Object != nullptr && Object->IsA(PSceneComponent::StaticClass()))
    {
        static_cast<PSceneComponent*>(Object)->SetWorldTransform(Transform);
        return true;
    }
    return false;
}

void FEditorTransformService::Reset()
{
    Targets.clear();
    InitialGizmoTransform = FTransform::Identity;
    CurrentGizmoTransform = FTransform::Identity;
    Mode = EEditorTransformMode::Select;
    CoordinateSpace = EEditorCoordinateSpace::World;
    bManipulating = false;
    bChanged = false;
}
}
