#include "Pico/Engine/AnimInstance.h"

#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Pico
{
namespace
{
constexpr float WalkSpeedThreshold = 5.0f;
constexpr float WalkStopDelaySeconds = 0.12f;

float MontageDuration(const FAnimationMontageData& Montage)
{
    return Montage.Segments.empty() ? 0.0f : Montage.Segments.back().EndTime;
}

const FAnimationMontageSection* FindSection(
    const FAnimationMontageData& Montage, float Time)
{
    const FAnimationMontageSection* Result = nullptr;
    for (const FAnimationMontageSection& Section : Montage.Sections)
    {
        if (Section.StartTime > Time + KindaSmallNumber) break;
        Result = &Section;
    }
    return Result;
}

const FAnimationMontageSection* FindSectionByName(
    const FAnimationMontageData& Montage, std::string_view Name)
{
    const auto Found = std::find_if(Montage.Sections.begin(), Montage.Sections.end(),
        [Name](const FAnimationMontageSection& Section) { return Section.Name == Name; });
    return Found != Montage.Sections.end() ? &*Found : nullptr;
}

float SectionEnd(const FAnimationMontageData& Montage, const FAnimationMontageSection* Section)
{
    if (Section == nullptr) return MontageDuration(Montage);
    const std::size_t Index = static_cast<std::size_t>(Section - Montage.Sections.data());
    return Index + 1 < Montage.Sections.size()
        ? Montage.Sections[Index + 1].StartTime : MontageDuration(Montage);
}

FQuat BlendQuat(const FQuat& A, const FQuat& B, float Alpha)
{
    const float Dot = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
    const float Sign = Dot < 0.0f ? -1.0f : 1.0f;
    return FQuat(
        A.X + (B.X * Sign - A.X) * Alpha,
        A.Y + (B.Y * Sign - A.Y) * Alpha,
        A.Z + (B.Z * Sign - A.Z) * Alpha,
        A.W + (B.W * Sign - A.W) * Alpha).GetNormalized();
}

void BlendPose(const FSkeletonData& Skeleton, const FSkeletonPose& Base,
    const FSkeletonPose& Overlay, float Weight, FSkeletonPose& OutPose)
{
    if (Base.LocalTransforms.size() != Overlay.LocalTransforms.size()) return;
    OutPose = Base;
    for (std::size_t Index = 0; Index < OutPose.LocalTransforms.size(); ++Index)
    {
        FTransform& Result = OutPose.LocalTransforms[Index];
        const FTransform& Target = Overlay.LocalTransforms[Index];
        Result.Translation = Result.Translation + (Target.Translation - Result.Translation) * Weight;
        Result.Scale = Result.Scale + (Target.Scale - Result.Scale) * Weight;
        Result.Rotation = BlendQuat(Result.Rotation, Target.Rotation, Weight);
    }
    RebuildPoseFromLocalTransforms(Skeleton, OutPose);
}
}

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
    LowGroundSpeedTime = 0.0f;
    PendingRootMotion = FTransform::Identity;
    if (Skeleton != nullptr)
    {
        BuildReferencePose(*Skeleton, CurrentPose);
        LocomotionPose = CurrentPose;
    }
}

bool PAnimInstance::PlayMontage(
    std::shared_ptr<const FAnimationMontageData> Montage,
    std::vector<std::shared_ptr<const FAnimationClipData>> SegmentClips,
    float PlayRate)
{
    if (Skeleton == nullptr || Montage == nullptr || !ValidateAnimationMontage(*Montage)
        || SegmentClips.size() != Montage->Segments.size() || !std::isfinite(PlayRate)
        || PlayRate <= 0.0f)
    {
        return false;
    }
    for (std::size_t Index = 0; Index < SegmentClips.size(); ++Index)
    {
        if (SegmentClips[Index] == nullptr
            || SegmentClips[Index]->SkeletonAsset != Montage->SkeletonAsset)
        {
            return false;
        }
    }
    if (ActiveMontage != nullptr) EndMontage(EMontageEndReason::Interrupted);
    ActiveMontage = std::move(Montage);
    MontageClips = std::move(SegmentClips);
    MontagePlaybackTime = ActiveMontage->Sections.front().StartTime;
    MontagePlayRate = PlayRate;
    MontageBlendWeight = ActiveMontage->BlendInTime <= 0.0f ? 1.0f : 0.0f;
    MontageBlendOutElapsed = 0.0f;
    bMontageBlendingOut = false;
    CurrentMontageSection = ActiveMontage->Sections.front().Name;
    return true;
}

bool PAnimInstance::StopMontage(EMontageEndReason Reason)
{
    if (ActiveMontage == nullptr) return false;
    BeginMontageBlendOut(Reason);
    return true;
}

bool PAnimInstance::JumpToSection(std::string_view SectionName)
{
    if (ActiveMontage == nullptr) return false;
    const FAnimationMontageSection* Section = FindSectionByName(*ActiveMontage, SectionName);
    if (Section == nullptr) return false;
    MontagePlaybackTime = Section->StartTime;
    CurrentMontageSection = Section->Name;
    PendingRootMotion = FTransform::Identity;
    return true;
}

bool PAnimInstance::IsMontagePlaying() const { return ActiveMontage != nullptr; }

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

    UpdateLocomotion(DeltaSeconds, GroundSpeed, bFalling);
    if (ActiveMontage != nullptr) UpdateMontage(DeltaSeconds);
}

void PAnimInstance::UpdateLocomotion(float DeltaSeconds, float GroundSpeed, bool bFalling)
{
    EAnimationState DesiredState = EAnimationState::Idle;
    if (bFalling)
    {
        LowGroundSpeedTime = 0.0f;
        DesiredState = EAnimationState::Jump;
    }
    else if (GroundSpeed > WalkSpeedThreshold)
    {
        LowGroundSpeedTime = 0.0f;
        DesiredState = EAnimationState::Walk;
    }
    else if (CurrentState == EAnimationState::Walk)
    {
        LowGroundSpeedTime += DeltaSeconds;
        DesiredState = LowGroundSpeedTime < WalkStopDelaySeconds
            ? EAnimationState::Walk : EAnimationState::Idle;
    }
    else
    {
        LowGroundSpeedTime = 0.0f;
    }
    if (DesiredState != CurrentState)
    {
        CurrentState = DesiredState;
        PlaybackTime = 0.0f;
        PendingRootMotion = FTransform::Identity;
    }

    const FAnimationClipData* Clip = SelectClip(CurrentState);
    if (Clip == nullptr)
    {
        BuildReferencePose(*Skeleton, LocomotionPose);
        CurrentPose = LocomotionPose;
        return;
    }

    const float PreviousTime = PlaybackTime;
    PlaybackTime += DeltaSeconds;
    PendingRootMotion = ExtractRootMotion(*Skeleton, *Clip, PreviousTime, PlaybackTime);
    SampleAnimationClip(*Skeleton, *Clip, PlaybackTime, LocomotionPose);
    CurrentPose = LocomotionPose;
    if (bExtractRootMotion
        && Clip->RootBoneIndex >= 0
        && static_cast<std::size_t>(Clip->RootBoneIndex) < LocomotionPose.LocalTransforms.size())
    {
        const std::size_t RootIndex = static_cast<std::size_t>(Clip->RootBoneIndex);
        LocomotionPose.LocalTransforms[RootIndex] = Skeleton->Bones[RootIndex].ReferenceLocalPose;
        RebuildPoseFromLocalTransforms(*Skeleton, LocomotionPose);
        CurrentPose = LocomotionPose;
    }
}

void PAnimInstance::UpdateMontage(float DeltaSeconds)
{
    PendingRootMotion = FTransform::Identity;
    if (bMontageBlendingOut)
    {
        MontageBlendOutElapsed += DeltaSeconds;
        const float Duration = ActiveMontage->BlendOutTime;
        MontageBlendWeight = Duration <= 0.0f
            ? 0.0f : std::max(1.0f - MontageBlendOutElapsed / Duration, 0.0f);
        if (MontageBlendWeight <= 0.0f)
        {
            EndMontage(PendingMontageEndReason);
            return;
        }
    }

    constexpr int32 MaxTransitionsPerUpdate = 32;
    float Remaining = bMontageBlendingOut ? 0.0f : DeltaSeconds * MontagePlayRate;
    int32 Transitions = 0;
    while (ActiveMontage != nullptr && Remaining > 0.0f && Transitions++ < MaxTransitionsPerUpdate)
    {
        const FAnimationMontageSection* Section = FindSection(*ActiveMontage, MontagePlaybackTime);
        if (Section == nullptr) { EndMontage(EMontageEndReason::Cancelled); return; }
        CurrentMontageSection = Section->Name;
        const float End = SectionEnd(*ActiveMontage, Section);
        const float Step = std::min(Remaining, std::max(End - MontagePlaybackTime, 0.0f));
        const float Previous = MontagePlaybackTime;
        MontagePlaybackTime += Step;
        Remaining -= Step;

        for (const FAnimationNotifyData& Notify : ActiveMontage->Notifies)
        {
            if (Notify.BeginTime > Previous && Notify.BeginTime <= MontagePlaybackTime)
                MontageNotifyDelegate.Broadcast(Notify.Name,
                    Notify.EndTime > Notify.BeginTime ? EMontageNotifyEvent::Begin : EMontageNotifyEvent::Trigger);
            if (Notify.EndTime > Notify.BeginTime
                && Notify.EndTime > Previous && Notify.EndTime <= MontagePlaybackTime)
                MontageNotifyDelegate.Broadcast(Notify.Name, EMontageNotifyEvent::End);
        }

        for (std::size_t Index = 0; Index < ActiveMontage->Segments.size(); ++Index)
        {
            const FAnimationMontageSegment& Segment = ActiveMontage->Segments[Index];
            const float RangeStart = std::max(Previous, Segment.StartTime);
            const float RangeEnd = std::min(MontagePlaybackTime, Segment.EndTime);
            if (RangeEnd > RangeStart && MontageClips[Index] != nullptr)
            {
                const float ClipStart = (RangeStart - Segment.StartTime) * Segment.PlayRate;
                const float ClipEnd = (RangeEnd - Segment.StartTime) * Segment.PlayRate;
                const FTransform Delta = ExtractRootMotion(
                    *Skeleton, *MontageClips[Index], ClipStart, ClipEnd);
                PendingRootMotion = PendingRootMotion * Delta;
            }
        }

        if (MontagePlaybackTime + KindaSmallNumber >= End)
        {
            if (Section->NextSection.empty())
            {
                BeginMontageBlendOut(EMontageEndReason::Completed);
                break;
            }
            const FAnimationMontageSection* Next = FindSectionByName(*ActiveMontage, Section->NextSection);
            if (Next == nullptr) { EndMontage(EMontageEndReason::Cancelled); break; }
            MontagePlaybackTime = Next->StartTime;
            CurrentMontageSection = Next->Name;
        }
    }
    if (ActiveMontage == nullptr) return;
    if (Transitions >= MaxTransitionsPerUpdate && Remaining > 0.0f)
    { EndMontage(EMontageEndReason::Cancelled); return; }

    const auto SegmentIt = std::find_if(ActiveMontage->Segments.begin(), ActiveMontage->Segments.end(),
        [this](const FAnimationMontageSegment& Segment)
        { return MontagePlaybackTime >= Segment.StartTime && MontagePlaybackTime <= Segment.EndTime; });
    if (SegmentIt == ActiveMontage->Segments.end()) return;
    const std::size_t SegmentIndex = static_cast<std::size_t>(SegmentIt - ActiveMontage->Segments.begin());
    FSkeletonPose MontagePose;
    const float ClipTime = (MontagePlaybackTime - SegmentIt->StartTime) * SegmentIt->PlayRate;
    if (!SampleAnimationClip(*Skeleton, *MontageClips[SegmentIndex], ClipTime, MontagePose)) return;
    if (!bMontageBlendingOut && ActiveMontage->BlendInTime > 0.0f && MontageBlendWeight < 1.0f)
        MontageBlendWeight = std::min(MontageBlendWeight + DeltaSeconds / ActiveMontage->BlendInTime, 1.0f);
    BlendPose(*Skeleton, LocomotionPose, MontagePose, MontageBlendWeight, CurrentPose);
    if (bExtractRootMotion && MontageClips[SegmentIndex]->RootBoneIndex >= 0)
    {
        const std::size_t Root = static_cast<std::size_t>(MontageClips[SegmentIndex]->RootBoneIndex);
        if (Root < CurrentPose.LocalTransforms.size())
        {
            CurrentPose.LocalTransforms[Root] = Skeleton->Bones[Root].ReferenceLocalPose;
            RebuildPoseFromLocalTransforms(*Skeleton, CurrentPose);
        }
    }
}

void PAnimInstance::BeginMontageBlendOut(EMontageEndReason Reason)
{
    if (ActiveMontage == nullptr || bMontageBlendingOut) return;
    if (ActiveMontage->BlendOutTime <= 0.0f)
    {
        EndMontage(Reason);
        return;
    }
    bMontageBlendingOut = true;
    MontageBlendOutElapsed = 0.0f;
    PendingMontageEndReason = Reason;
}

void PAnimInstance::EndMontage(EMontageEndReason Reason)
{
    ActiveMontage.reset(); MontageClips.clear(); CurrentMontageSection.clear();
    MontagePlaybackTime = 0.0f; MontageBlendWeight = 0.0f; LastMontageEndReason = Reason;
    MontageBlendOutElapsed = 0.0f; bMontageBlendingOut = false;
    CurrentPose = LocomotionPose;
    MontageEndedDelegate.Broadcast(Reason);
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

float PAnimInstance::GetMontagePlaybackTime() const { return MontagePlaybackTime; }
std::string_view PAnimInstance::GetCurrentMontageSection() const { return CurrentMontageSection; }
EMontageEndReason PAnimInstance::GetLastMontageEndReason() const { return LastMontageEndReason; }
TObjectMulticastDelegate<void(EMontageEndReason)>& PAnimInstance::OnMontageEnded() { return MontageEndedDelegate; }
TObjectMulticastDelegate<void(std::string_view, EMontageNotifyEvent)>& PAnimInstance::OnMontageNotify()
{ return MontageNotifyDelegate; }

void PAnimInstance::BeginDestroy()
{
    if (ActiveMontage != nullptr) EndMontage(EMontageEndReason::Cancelled);
    MontageEndedDelegate.Clear(); MontageNotifyDelegate.Clear();
    PObject::BeginDestroy();
}
}
