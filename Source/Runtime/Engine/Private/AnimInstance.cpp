#include "Pico/Engine/AnimInstance.h"

#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PAnimInstance)

const char* ToString(EAnimationState State)
{
    switch (State)
    {
    case EAnimationState::Idle: return "Idle";
    case EAnimationState::Walk: return "Walk";
    case EAnimationState::Jump: return "Jump";
    }
    return "Unknown";
}

PAnimInstance::PAnimInstance(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

void PAnimInstance::SetAnimationSet(
    std::shared_ptr<const FSkeletonData> InSkeleton,
    std::shared_ptr<const FAnimationClipData> InIdle,
    std::shared_ptr<const FAnimationClipData> InWalk,
    std::shared_ptr<const FAnimationClipData> InJump)
{
    Skeleton = std::move(InSkeleton);
    IdleClip = std::move(InIdle);
    WalkClip = std::move(InWalk);
    JumpClip = std::move(InJump);
    CurrentState = EAnimationState::Idle;
    PlaybackTime = 0.0f;
    PendingRootMotion = FTransform::Identity;
    if (Skeleton != nullptr)
    {
        BuildReferencePose(*Skeleton, CurrentPose);
    }
}

const FAnimationClipData* PAnimInstance::SelectClip(EAnimationState State) const
{
    switch (State)
    {
    case EAnimationState::Idle: return IdleClip.get();
    case EAnimationState::Walk: return WalkClip.get();
    case EAnimationState::Jump: return JumpClip.get();
    }
    return nullptr;
}

void PAnimInstance::Update(float DeltaSeconds, float GroundSpeed, bool bFalling)
{
    if (Skeleton == nullptr || !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const EAnimationState DesiredState = bFalling
        ? EAnimationState::Jump
        : (GroundSpeed > 5.0f ? EAnimationState::Walk : EAnimationState::Idle);
    if (DesiredState != CurrentState)
    {
        CurrentState = DesiredState;
        PlaybackTime = 0.0f;
        PendingRootMotion = FTransform::Identity;
    }

    const FAnimationClipData* Clip = SelectClip(CurrentState);
    if (Clip == nullptr)
    {
        BuildReferencePose(*Skeleton, CurrentPose);
        return;
    }

    const float PreviousTime = PlaybackTime;
    PlaybackTime += DeltaSeconds;
    PendingRootMotion = ExtractRootMotion(*Skeleton, *Clip, PreviousTime, PlaybackTime);
    SampleAnimationClip(*Skeleton, *Clip, PlaybackTime, CurrentPose);
    if (bExtractRootMotion
        && Clip->RootBoneIndex >= 0
        && static_cast<std::size_t>(Clip->RootBoneIndex) < CurrentPose.LocalTransforms.size())
    {
        const std::size_t RootIndex = static_cast<std::size_t>(Clip->RootBoneIndex);
        CurrentPose.LocalTransforms[RootIndex] = Skeleton->Bones[RootIndex].ReferenceLocalPose;
        RebuildPoseFromLocalTransforms(*Skeleton, CurrentPose);
    }
}

bool PAnimInstance::SetPlaybackTime(float TimeSeconds)
{
    const FAnimationClipData* Clip = SelectClip(CurrentState);
    if (Skeleton == nullptr || Clip == nullptr || !std::isfinite(TimeSeconds))
    {
        return false;
    }
    PlaybackTime = std::max(TimeSeconds, 0.0f);
    PendingRootMotion = FTransform::Identity;
    return SampleAnimationClip(*Skeleton, *Clip, PlaybackTime, CurrentPose);
}

void PAnimInstance::SetExtractRootMotion(bool bValue) { bExtractRootMotion = bValue; }

EAnimationState PAnimInstance::GetAnimationState() const { return CurrentState; }
float PAnimInstance::GetPlaybackTime() const { return PlaybackTime; }
const FSkeletonPose& PAnimInstance::GetPose() const { return CurrentPose; }
const FAnimationClipData* PAnimInstance::GetCurrentClip() const { return SelectClip(CurrentState); }

FTransform PAnimInstance::ConsumeExtractedRootMotion()
{
    const FTransform Result = PendingRootMotion;
    PendingRootMotion = FTransform::Identity;
    return Result;
}
}
