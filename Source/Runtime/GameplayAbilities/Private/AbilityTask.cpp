#include "Pico/GameplayAbilities/AbilityTask.h"

#include "Pico/Core/GameThread.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PAbilityTask)

bool PAbilityTask::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, TaskStateValue, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, EndReasonValue, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PAbilityTask::PAbilityTask(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

bool PAbilityTask::InitializeTask(
    PGameplayAbilitySystemComponent* InAbilitySystem,
    FGameplayAbilitySpecHandle InAbilityHandle,
    FAbilityTaskHandle InTaskHandle)
{
    if (InAbilitySystem == nullptr
        || !InAbilityHandle.IsValid()
        || !InTaskHandle.IsValid()
        || AbilitySystem.Get() != nullptr
        || TaskHandle.IsValid())
    {
        return false;
    }
    AbilitySystem = InAbilitySystem;
    OwningAbilityHandle = InAbilityHandle;
    TaskHandle = InTaskHandle;
    TaskStateValue = static_cast<int32>(EAbilityTaskState::Created);
    return true;
}

bool PAbilityTask::ReadyForActivation()
{
    if (!CheckGameThread("PAbilityTask::ReadyForActivation")
        || GetTaskState() != EAbilityTaskState::Created
        || GetAbilitySystemComponent() == nullptr)
    {
        return false;
    }
    TaskStateValue = static_cast<int32>(EAbilityTaskState::ReadyForActivation);
    if (!ActivateTask())
    {
        FinishTask(EAbilityTaskEndReason::Failed);
        return false;
    }
    if (GetTaskState() == EAbilityTaskState::ReadyForActivation)
    {
        TaskStateValue = static_cast<int32>(EAbilityTaskState::Active);
    }
    return GetTaskState() == EAbilityTaskState::Active || IsFinished();
}

bool PAbilityTask::ExternalCancel()
{
    return FinishTask(EAbilityTaskEndReason::Cancelled);
}

bool PAbilityTask::EndTask()
{
    return FinishTask(EAbilityTaskEndReason::Completed);
}

FAbilityTaskHandle PAbilityTask::GetTaskHandle() const { return TaskHandle; }
FGameplayAbilitySpecHandle PAbilityTask::GetOwningAbilityHandle() const
{
    return OwningAbilityHandle;
}
PGameplayAbilitySystemComponent* PAbilityTask::GetAbilitySystemComponent() const
{
    return AbilitySystem.Get();
}
EAbilityTaskState PAbilityTask::GetTaskState() const
{
    return static_cast<EAbilityTaskState>(TaskStateValue);
}
EAbilityTaskEndReason PAbilityTask::GetEndReason() const
{
    return static_cast<EAbilityTaskEndReason>(EndReasonValue);
}
bool PAbilityTask::IsActive() const
{
    return GetTaskState() == EAbilityTaskState::Active;
}
bool PAbilityTask::IsFinished() const
{
    return GetTaskState() == EAbilityTaskState::Finished
        || GetTaskState() == EAbilityTaskState::Cancelled
        || GetTaskState() == EAbilityTaskState::Destroyed;
}
FOnAbilityTaskEnded& PAbilityTask::OnTaskEnded() { return TaskEndedEvent; }

bool PAbilityTask::ActivateTask() { return true; }
void PAbilityTask::TickTask(float) {}
void PAbilityTask::OnTaskEnding(EAbilityTaskEndReason) {}

bool PAbilityTask::FinishTask(EAbilityTaskEndReason Reason)
{
    if (!CheckGameThread("PAbilityTask::FinishTask") || IsFinished()) return false;
    EndReasonValue = static_cast<int32>(Reason);
    TaskStateValue = static_cast<int32>(
        Reason == EAbilityTaskEndReason::Cancelled
            || Reason == EAbilityTaskEndReason::OwnerEnded
        ? EAbilityTaskState::Cancelled
        : EAbilityTaskState::Finished);
    OnTaskEnding(Reason);
    TaskEndedEvent.Broadcast(Reason);
    if (PGameplayAbilitySystemComponent* Component = GetAbilitySystemComponent())
    {
        Component->NotifyAbilityTaskEnded(TaskHandle);
    }
    return true;
}

void PAbilityTask::BeginDestroy()
{
    if (!IsFinished())
    {
        EndReasonValue = static_cast<int32>(EAbilityTaskEndReason::OwnerEnded);
        TaskStateValue = static_cast<int32>(EAbilityTaskState::Cancelled);
        OnTaskEnding(EAbilityTaskEndReason::OwnerEnded);
    }
    TaskStateValue = static_cast<int32>(EAbilityTaskState::Destroyed);
    TaskEndedEvent.Clear();
    AbilitySystem.Reset();
    OwningAbilityHandle.Reset();
    TaskHandle.Reset();
    PObject::BeginDestroy();
}

PICO_DEFINE_CLASS(PAbilityTaskWaitDelay)

bool PAbilityTaskWaitDelay::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Duration, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, RemainingTime, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PAbilityTaskWaitDelay::PAbilityTaskWaitDelay(const FObjectConstructionParams& Params)
    : PAbilityTask(Params)
{
}

bool PAbilityTaskWaitDelay::Configure(float InDuration)
{
    if (!std::isfinite(InDuration) || InDuration < 0.0f) return false;
    Duration = InDuration;
    RemainingTime = InDuration;
    return true;
}

float PAbilityTaskWaitDelay::GetDuration() const { return Duration; }
float PAbilityTaskWaitDelay::GetRemainingTime() const { return RemainingTime; }
TObjectMulticastDelegate<void()>& PAbilityTaskWaitDelay::OnFinish() { return FinishEvent; }

bool PAbilityTaskWaitDelay::ActivateTask()
{
    if (RemainingTime <= 0.0f) FinishTask(EAbilityTaskEndReason::Completed);
    return true;
}

void PAbilityTaskWaitDelay::TickTask(float DeltaSeconds)
{
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f || !IsActive()) return;
    RemainingTime = std::max(0.0f, RemainingTime - DeltaSeconds);
    if (RemainingTime <= 0.0f) FinishTask(EAbilityTaskEndReason::Completed);
}

void PAbilityTaskWaitDelay::OnTaskEnding(EAbilityTaskEndReason Reason)
{
    if (Reason == EAbilityTaskEndReason::Completed) FinishEvent.Broadcast();
    FinishEvent.Clear();
}

PICO_DEFINE_CLASS_NO_PROPERTIES(PAbilityTaskWaitGameplayEvent)

PAbilityTaskWaitGameplayEvent::PAbilityTaskWaitGameplayEvent(
    const FObjectConstructionParams& Params)
    : PAbilityTask(Params)
{
}

bool PAbilityTaskWaitGameplayEvent::Configure(
    const FGameplayTag& InEventTag,
    bool bInExactMatch)
{
    if (!InEventTag.IsValid()) return false;
    EventTag = InEventTag;
    bExactMatch = bInExactMatch;
    return true;
}

const FGameplayTag& PAbilityTaskWaitGameplayEvent::GetEventTag() const { return EventTag; }
bool PAbilityTaskWaitGameplayEvent::UsesExactMatch() const { return bExactMatch; }
const FGameplayEventData& PAbilityTaskWaitGameplayEvent::GetLastEventData() const
{
    return LastEventData;
}
TObjectMulticastDelegate<void(const FGameplayEventData&)>&
PAbilityTaskWaitGameplayEvent::OnEventReceived()
{
    return EventReceivedEvent;
}

bool PAbilityTaskWaitGameplayEvent::ActivateTask()
{
    PGameplayAbilitySystemComponent* Component = GetAbilitySystemComponent();
    return Component != nullptr
        && Component->OnGameplayEvent().AddObject(
            this, &PAbilityTaskWaitGameplayEvent::HandleGameplayEvent).IsValid();
}

void PAbilityTaskWaitGameplayEvent::OnTaskEnding(EAbilityTaskEndReason Reason)
{
    if (PGameplayAbilitySystemComponent* Component = GetAbilitySystemComponent())
    {
        Component->OnGameplayEvent().RemoveAll(this);
    }
    if (Reason == EAbilityTaskEndReason::Completed)
    {
        EventReceivedEvent.Broadcast(LastEventData);
    }
    EventReceivedEvent.Clear();
}

void PAbilityTaskWaitGameplayEvent::HandleGameplayEvent(
    const FGameplayEventData& EventData)
{
    if (!IsActive()) return;
    const bool bMatches = bExactMatch
        ? EventData.EventTag.MatchesTagExact(EventTag)
        : EventData.EventTag.MatchesTag(EventTag);
    if (!bMatches) return;
    LastEventData = EventData;
    FinishTask(EAbilityTaskEndReason::Completed);
}

PICO_DEFINE_CLASS_NO_PROPERTIES(PAbilityTaskPlayAnimationAndWait)

PAbilityTaskPlayAnimationAndWait::PAbilityTaskPlayAnimationAndWait(
    const FObjectConstructionParams& Params)
    : PAbilityTask(Params)
{
}

bool PAbilityTaskPlayAnimationAndWait::Configure(
    PAnimInstance* InAnimInstance,
    std::shared_ptr<const FAnimationMontageData> InMontage,
    std::vector<std::shared_ptr<const FAnimationClipData>> InSegmentClips,
    float InPlayRate)
{
    if (InAnimInstance == nullptr
        || InMontage == nullptr
        || !std::isfinite(InPlayRate)
        || InPlayRate <= 0.0f)
    {
        return false;
    }
    AnimInstance = InAnimInstance;
    Montage = std::move(InMontage);
    SegmentClips = std::move(InSegmentClips);
    PlayRate = InPlayRate;
    return true;
}

PAnimInstance* PAbilityTaskPlayAnimationAndWait::GetAnimInstance() const
{
    return AnimInstance.Get();
}
float PAbilityTaskPlayAnimationAndWait::GetPlayRate() const { return PlayRate; }
EMontageEndReason PAbilityTaskPlayAnimationAndWait::GetMontageEndReason() const
{
    return MontageEndReason;
}
TObjectMulticastDelegate<void(EMontageEndReason)>&
PAbilityTaskPlayAnimationAndWait::OnAnimationEnded()
{
    return AnimationEndedEvent;
}
TObjectMulticastDelegate<void(std::string_view, EMontageNotifyEvent)>&
PAbilityTaskPlayAnimationAndWait::OnNotify()
{
    return NotifyEvent;
}

bool PAbilityTaskPlayAnimationAndWait::ActivateTask()
{
    PAnimInstance* Instance = GetAnimInstance();
    if (Instance == nullptr
        || !Instance->PlayMontage(Montage, SegmentClips, PlayRate))
    {
        return false;
    }
    bStartedMontage = true;
    const FDelegateHandle EndedHandle = Instance->OnMontageEnded().AddObject(
        this, &PAbilityTaskPlayAnimationAndWait::HandleMontageEnded);
    const FDelegateHandle NotifyHandle = Instance->OnMontageNotify().AddObject(
        this, &PAbilityTaskPlayAnimationAndWait::HandleMontageNotify);
    if (!EndedHandle.IsValid() || !NotifyHandle.IsValid())
    {
        Instance->OnMontageEnded().RemoveAll(this);
        Instance->OnMontageNotify().RemoveAll(this);
        MontageEndReason = EMontageEndReason::Cancelled;
        Instance->StopMontage(EMontageEndReason::Cancelled);
        return false;
    }
    return true;
}

void PAbilityTaskPlayAnimationAndWait::OnTaskEnding(EAbilityTaskEndReason Reason)
{
    PAnimInstance* Instance = GetAnimInstance();
    if (!bReceivedMontageEnd)
    {
        MontageEndReason = Reason == EAbilityTaskEndReason::Interrupted
            ? EMontageEndReason::Interrupted
            : EMontageEndReason::Cancelled;
    }
    if (Instance != nullptr)
    {
        Instance->OnMontageEnded().RemoveAll(this);
        Instance->OnMontageNotify().RemoveAll(this);
        if (bStartedMontage && Instance->IsMontagePlaying() && !bReceivedMontageEnd)
        {
            Instance->StopMontage(MontageEndReason);
        }
    }
    AnimationEndedEvent.Broadcast(MontageEndReason);
    AnimationEndedEvent.Clear();
    NotifyEvent.Clear();
    Montage.reset();
    SegmentClips.clear();
    bStartedMontage = false;
}

void PAbilityTaskPlayAnimationAndWait::HandleMontageEnded(EMontageEndReason Reason)
{
    if (!IsActive()) return;
    MontageEndReason = Reason;
    bReceivedMontageEnd = true;
    switch (Reason)
    {
    case EMontageEndReason::Completed:
        FinishTask(EAbilityTaskEndReason::Completed);
        break;
    case EMontageEndReason::Interrupted:
        FinishTask(EAbilityTaskEndReason::Interrupted);
        break;
    case EMontageEndReason::Cancelled:
        FinishTask(EAbilityTaskEndReason::Cancelled);
        break;
    }
}

void PAbilityTaskPlayAnimationAndWait::HandleMontageNotify(
    std::string_view NotifyName,
    EMontageNotifyEvent Event)
{
    if (IsActive()) NotifyEvent.Broadcast(NotifyName, Event);
}
}
