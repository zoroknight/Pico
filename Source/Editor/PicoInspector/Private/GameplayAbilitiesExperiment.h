#pragma once

#include "InspectorExperiment.h"

#include "Pico/Core/Delegate.h"
#include "Pico/GameplayAbilities/GameplayAbilityPrediction.h"
#include "Pico/Object/ObjectTypes.h"

#include <string>
#include <memory>
#include <vector>

namespace Pico
{
class PActor;
class PGameplayAbilitySystemComponent;
class PAnimInstance;
struct FGameplayAttributeChangeData;
struct FGameplayEventData;
struct FGameplayAbilitySpecHandle;
struct FGameplayEffectSpec;
struct FActiveGameplayEffectHandle;
enum class EGameplayAbilityEvent : uint8;
enum class EAbilityTaskEndReason : uint8;
enum class EMontageEndReason : uint8;
enum class EMontageNotifyEvent : uint8;
struct FSkeletonData;
struct FAnimationClipData;
struct FAnimationMontageData;

class FGameplayAbilitiesExperiment final : public IInspectorExperiment
{
public:
    ~FGameplayAbilitiesExperiment() override;

    const char* GetName() const override;
    bool SetUp() override;
    void Draw() override;
    void Reset() override;
    void TearDown() override;

private:
    PActor* GetOwner() const;
    PGameplayAbilitySystemComponent* GetAbilitySystem() const;
    PAnimInstance* GetAnimInstance() const;
    void GrantAbility();
    void ActivateAbility();
    void CancelAbility();
    void RemoveAbility();
    void ApplyDamage();
    void ApplyRegeneration();
    void ApplyStun();
    void AdvanceEffects();
    void BeginDashPrediction();
    void ResolveDashPrediction(bool bAccepted);
    void RemoveSelectedEffect();
    void CreateDelayTask();
    void CreateWaitEventTask();
    void SendTaskEvent();
    void CreateAnimationTask();
    void InterruptAnimationTask();
    void CancelSelectedTask();
    void AdvanceTasks();
    void HandleAbilityEvent(FGameplayAbilitySpecHandle Handle, EGameplayAbilityEvent Event);
    void HandleEffectApplied(
        const FGameplayEffectSpec& Spec,
        FActiveGameplayEffectHandle Handle);
    void HandleEffectRemoved(
        const FGameplayEffectSpec& Spec,
        FActiveGameplayEffectHandle Handle);
    void HandleAttributeChanged(const FGameplayAttributeChangeData& Change);
    void AddLog(std::string Message);

    FObjectHandle OwnerHandle;
    FObjectHandle AbilitySystemHandle;
    FObjectHandle AnimInstanceHandle;
    uint32 SelectedAbilityHandle = 0;
    uint32 SelectedEffectHandle = 0;
    uint32 SelectedTaskHandle = 0;
    int Damage = 20;
    float AdvanceSeconds = 1.0f;
    float DelaySeconds = 2.0f;
    float EventMagnitude = 1.0f;
    FGameplayPredictionLedger PredictionLedger;
    FGameplayPredictionKey PendingPredictionKey;
    FVector3 PredictedLocation;
    float PredictedMana = 100.0f;
    std::string PredictionStatus = "Idle";
    std::shared_ptr<const FSkeletonData> TaskSkeleton;
    std::shared_ptr<const FAnimationClipData> TaskClip;
    std::shared_ptr<const FAnimationMontageData> TaskMontage;
    std::vector<std::string> EventLog;
};
}
