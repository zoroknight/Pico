#pragma once

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/ReflectionMacros.h"

#include <memory>
#include <string_view>
#include <vector>

namespace Pico
{
class PGameplayAbilitySystemComponent;

struct FAbilityTaskHandle
{
    uint32 Value = 0;

    bool IsValid() const { return Value != 0; }
    void Reset() { Value = 0; }
    friend bool operator==(const FAbilityTaskHandle&, const FAbilityTaskHandle&) = default;
};

enum class EAbilityTaskState : uint8
{
    Created,
    ReadyForActivation,
    Active,
    Finished,
    Cancelled,
    Destroyed
};

enum class EAbilityTaskEndReason : uint8
{
    Completed,
    Cancelled,
    OwnerEnded,
    Interrupted,
    Failed
};

struct FGameplayEventData
{
    FGameplayTag EventTag;
    FObjectHandle InstigatorHandle;
    FObjectHandle TargetHandle;
    float EventMagnitude = 0.0f;
    FName PayloadName;
};

using FOnAbilityTaskEnded = TObjectMulticastDelegate<void(EAbilityTaskEndReason)>;

class PAbilityTask : public PObject
{
    PICO_DECLARE_CLASS(PAbilityTask, PObject)

public:
    bool ReadyForActivation();
    bool ExternalCancel();
    bool EndTask();

    FAbilityTaskHandle GetTaskHandle() const;
    FGameplayAbilitySpecHandle GetOwningAbilityHandle() const;
    PGameplayAbilitySystemComponent* GetAbilitySystemComponent() const;
    EAbilityTaskState GetTaskState() const;
    EAbilityTaskEndReason GetEndReason() const;
    bool IsActive() const;
    bool IsFinished() const;
    FOnAbilityTaskEnded& OnTaskEnded();

protected:
    explicit PAbilityTask(const FObjectConstructionParams& Params);
    virtual bool ActivateTask();
    virtual void TickTask(float DeltaSeconds);
    virtual void OnTaskEnding(EAbilityTaskEndReason Reason);
    bool FinishTask(EAbilityTaskEndReason Reason);
    void BeginDestroy() override;

private:
    friend class PGameplayAbilitySystemComponent;

    bool InitializeTask(
        PGameplayAbilitySystemComponent* InAbilitySystem,
        FGameplayAbilitySpecHandle InAbilityHandle,
        FAbilityTaskHandle InTaskHandle);

    TWeakObjectPtr<PGameplayAbilitySystemComponent> AbilitySystem;
    FGameplayAbilitySpecHandle OwningAbilityHandle;
    FAbilityTaskHandle TaskHandle;
    int32 TaskStateValue = static_cast<int32>(EAbilityTaskState::Created);
    int32 EndReasonValue = static_cast<int32>(EAbilityTaskEndReason::Completed);
    FOnAbilityTaskEnded TaskEndedEvent;
};

class PAbilityTaskWaitDelay final : public PAbilityTask
{
    PICO_DECLARE_CLASS(PAbilityTaskWaitDelay, PAbilityTask)

public:
    float GetDuration() const;
    float GetRemainingTime() const;
    TObjectMulticastDelegate<void()>& OnFinish();

protected:
    explicit PAbilityTaskWaitDelay(const FObjectConstructionParams& Params);
    bool ActivateTask() override;
    void TickTask(float DeltaSeconds) override;
    void OnTaskEnding(EAbilityTaskEndReason Reason) override;

private:
    friend class PGameplayAbilitySystemComponent;
    bool Configure(float InDuration);

    float Duration = 0.0f;
    float RemainingTime = 0.0f;
    TObjectMulticastDelegate<void()> FinishEvent;
};

class PAbilityTaskWaitGameplayEvent final : public PAbilityTask
{
    PICO_DECLARE_CLASS(PAbilityTaskWaitGameplayEvent, PAbilityTask)

public:
    const FGameplayTag& GetEventTag() const;
    bool UsesExactMatch() const;
    const FGameplayEventData& GetLastEventData() const;
    TObjectMulticastDelegate<void(const FGameplayEventData&)>& OnEventReceived();

protected:
    explicit PAbilityTaskWaitGameplayEvent(const FObjectConstructionParams& Params);
    bool ActivateTask() override;
    void OnTaskEnding(EAbilityTaskEndReason Reason) override;

private:
    friend class PGameplayAbilitySystemComponent;
    bool Configure(const FGameplayTag& InEventTag, bool bInExactMatch);
    void HandleGameplayEvent(const FGameplayEventData& EventData);

    FGameplayTag EventTag;
    bool bExactMatch = false;
    FGameplayEventData LastEventData;
    TObjectMulticastDelegate<void(const FGameplayEventData&)> EventReceivedEvent;
};

class PAbilityTaskPlayAnimationAndWait final : public PAbilityTask
{
    PICO_DECLARE_CLASS(PAbilityTaskPlayAnimationAndWait, PAbilityTask)

public:
    PAnimInstance* GetAnimInstance() const;
    float GetPlayRate() const;
    EMontageEndReason GetMontageEndReason() const;
    TObjectMulticastDelegate<void(EMontageEndReason)>& OnAnimationEnded();
    TObjectMulticastDelegate<void(std::string_view, EMontageNotifyEvent)>& OnNotify();

protected:
    explicit PAbilityTaskPlayAnimationAndWait(const FObjectConstructionParams& Params);
    bool ActivateTask() override;
    void OnTaskEnding(EAbilityTaskEndReason Reason) override;

private:
    friend class PGameplayAbilitySystemComponent;
    bool Configure(
        PAnimInstance* InAnimInstance,
        std::shared_ptr<const FAnimationMontageData> InMontage,
        std::vector<std::shared_ptr<const FAnimationClipData>> InSegmentClips,
        float InPlayRate);
    void HandleMontageEnded(EMontageEndReason Reason);
    void HandleMontageNotify(std::string_view NotifyName, EMontageNotifyEvent Event);

    TWeakObjectPtr<PAnimInstance> AnimInstance;
    std::shared_ptr<const FAnimationMontageData> Montage;
    std::vector<std::shared_ptr<const FAnimationClipData>> SegmentClips;
    float PlayRate = 1.0f;
    EMontageEndReason MontageEndReason = EMontageEndReason::Completed;
    bool bStartedMontage = false;
    bool bReceivedMontageEnd = false;
    TObjectMulticastDelegate<void(EMontageEndReason)> AnimationEndedEvent;
    TObjectMulticastDelegate<void(std::string_view, EMontageNotifyEvent)> NotifyEvent;
};
}
