#pragma once

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Object/ObjectDelegate.h"
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

enum class EMontageNotifyEvent : uint8
{
    Trigger,
    Begin,
    End
};

enum class EMontageEndReason : uint8
{
    Completed,
    Interrupted,
    Cancelled
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
    bool PlayMontage(
        std::shared_ptr<const FAnimationMontageData> Montage,
        std::vector<std::shared_ptr<const FAnimationClipData>> SegmentClips,
        float PlayRate = 1.0f);
    bool StopMontage(EMontageEndReason Reason = EMontageEndReason::Interrupted);
    bool JumpToSection(std::string_view SectionName);
    bool IsMontagePlaying() const;
    void Update(float DeltaSeconds, float GroundSpeed, bool bFalling);
    bool SetPlaybackTime(float TimeSeconds);
    void SetExtractRootMotion(bool bValue);

    EAnimationState GetAnimationState() const;
    float GetPlaybackTime() const;
    const FSkeletonPose& GetPose() const;
    const FAnimationClipData* GetCurrentClip() const;
    FTransform ConsumeExtractedRootMotion();
    float GetMontagePlaybackTime() const;
    std::string_view GetCurrentMontageSection() const;
    EMontageEndReason GetLastMontageEndReason() const;

    TObjectMulticastDelegate<void(EMontageEndReason)>& OnMontageEnded();
    TObjectMulticastDelegate<void(std::string_view, EMontageNotifyEvent)>& OnMontageNotify();

protected:
    explicit PAnimInstance(const FObjectConstructionParams& Params);

private:
    const FAnimationClipData* SelectClip(EAnimationState State) const;
    void UpdateLocomotion(float DeltaSeconds, float GroundSpeed, bool bFalling);
    void UpdateMontage(float DeltaSeconds);
    void BeginMontageBlendOut(EMontageEndReason Reason);
    void EndMontage(EMontageEndReason Reason);
    void BeginDestroy() override;

    std::shared_ptr<const FSkeletonData> Skeleton;
    std::shared_ptr<const FAnimationClipData> IdleClip;
    std::shared_ptr<const FAnimationClipData> WalkClip;
    std::shared_ptr<const FAnimationClipData> JumpClip;
    FSkeletonPose CurrentPose;
    FTransform PendingRootMotion;
    EAnimationState CurrentState = EAnimationState::Idle;
    float PlaybackTime = 0.0f;
    float LowGroundSpeedTime = 0.0f;
    bool bExtractRootMotion = false;

    std::shared_ptr<const FAnimationMontageData> ActiveMontage;
    std::vector<std::shared_ptr<const FAnimationClipData>> MontageClips;
    FSkeletonPose LocomotionPose;
    float MontagePlaybackTime = 0.0f;
    float MontagePlayRate = 1.0f;
    float MontageBlendWeight = 0.0f;
    float MontageBlendOutElapsed = 0.0f;
    bool bMontageBlendingOut = false;
    EMontageEndReason PendingMontageEndReason = EMontageEndReason::Completed;
    std::string CurrentMontageSection;
    EMontageEndReason LastMontageEndReason = EMontageEndReason::Completed;
    TObjectMulticastDelegate<void(EMontageEndReason)> MontageEndedDelegate;
    TObjectMulticastDelegate<void(std::string_view, EMontageNotifyEvent)> MontageNotifyDelegate;
};
}
