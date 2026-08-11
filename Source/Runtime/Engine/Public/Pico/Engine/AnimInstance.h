#pragma once

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Object/ReflectionMacros.h"

#include <memory>

namespace Pico
{
enum class EAnimationState : uint8
{
    Idle,
    Walk,
    Jump
};

const char* ToString(EAnimationState State);

class PAnimInstance : public PObject
{
    PICO_DECLARE_CLASS(PAnimInstance, PObject)

public:
    void SetAnimationSet(
        std::shared_ptr<const FSkeletonData> InSkeleton,
        std::shared_ptr<const FAnimationClipData> InIdle,
        std::shared_ptr<const FAnimationClipData> InWalk,
        std::shared_ptr<const FAnimationClipData> InJump);
    void Update(float DeltaSeconds, float GroundSpeed, bool bFalling);
    bool SetPlaybackTime(float TimeSeconds);
    void SetExtractRootMotion(bool bValue);

    EAnimationState GetAnimationState() const;
    float GetPlaybackTime() const;
    const FSkeletonPose& GetPose() const;
    const FAnimationClipData* GetCurrentClip() const;
    FTransform ConsumeExtractedRootMotion();

protected:
    explicit PAnimInstance(const FObjectConstructionParams& Params);

private:
    const FAnimationClipData* SelectClip(EAnimationState State) const;

    std::shared_ptr<const FSkeletonData> Skeleton;
    std::shared_ptr<const FAnimationClipData> IdleClip;
    std::shared_ptr<const FAnimationClipData> WalkClip;
    std::shared_ptr<const FAnimationClipData> JumpClip;
    FSkeletonPose CurrentPose;
    FTransform PendingRootMotion;
    EAnimationState CurrentState = EAnimationState::Idle;
    float PlaybackTime = 0.0f;
    bool bExtractRootMotion = false;
};
}
