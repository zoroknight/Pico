#pragma once

#include "Pico/Core/Delegate.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/GameplayAbilities/AbilityTask.h"
#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"
#include "Pico/Object/ObjectPtr.h"

#include <map>
#include <string>
#include <vector>

namespace Pico
{
class PActor;

enum class EGameplayAbilityEvent : uint8
{
    Granted,
    Activated,
    Cancelled,
    Ended,
    Removed
};

using FOnGameplayAbilityEvent =
    TMulticastDelegate<void(FGameplayAbilitySpecHandle, EGameplayAbilityEvent)>;
using FOnGameplayTagChanged = TMulticastDelegate<void(const FGameplayTag&, int32)>;
using FOnGameplayEffectApplied =
    TMulticastDelegate<void(const FGameplayEffectSpec&, FActiveGameplayEffectHandle)>;
using FOnGameplayEffectRemoved =
    TMulticastDelegate<void(const FGameplayEffectSpec&, FActiveGameplayEffectHandle)>;

class PGameplayAbilitySystemComponent : public PActorComponent
{
    PICO_DECLARE_CLASS(PGameplayAbilitySystemComponent, PActorComponent)

public:
    bool InitAbilityActorInfo(PActor* InOwnerActor, PActor* InAvatarActor);
    PActor* GetAbilityOwnerActor() const;
    PActor* GetAbilityAvatarActor() const;
    PAttributeSet* GetAttributeSet() const;

    FGameplayAbilitySpecHandle GiveAbility(
        const PClass* AbilityClass,
        int32 Level = 1,
        int32 InputId = -1);
    bool ConfigureAbilitySpec(
        FGameplayAbilitySpecHandle Handle,
        float CostOverride,
        float CooldownOverride);
    bool ClearAbility(FGameplayAbilitySpecHandle Handle);
    bool TryActivateAbility(FGameplayAbilitySpecHandle Handle);
    bool CancelAbility(FGameplayAbilitySpecHandle Handle);
    bool EndAbility(FGameplayAbilitySpecHandle Handle, bool bWasCancelled = false);
    void CancelAllAbilities();
    void ClearAllAbilities();

    FGameplayAbilitySpec* FindAbilitySpec(FGameplayAbilitySpecHandle Handle);
    const FGameplayAbilitySpec* FindAbilitySpec(FGameplayAbilitySpecHandle Handle) const;
    const std::vector<FGameplayAbilitySpec>& GetActivatableAbilities() const;

    FGameplayEffectSpec MakeOutgoingSpec(
        const PClass* EffectClass,
        float Level = 1.0f,
        PObject* SourceObject = nullptr) const;
    bool ApplyGameplayEffectSpecToSelf(
        const FGameplayEffectSpec& Spec,
        FActiveGameplayEffectHandle* OutHandle = nullptr);
    bool RemoveActiveGameplayEffect(FActiveGameplayEffectHandle Handle);
    void RemoveAllActiveGameplayEffects();
    void TickActiveGameplayEffects(float DeltaSeconds);
    const FActiveGameplayEffect* FindActiveGameplayEffect(
        FActiveGameplayEffectHandle Handle) const;
    const std::vector<FActiveGameplayEffect>& GetActiveGameplayEffects() const;

    PAbilityTaskWaitDelay* CreateWaitDelayTask(
        FGameplayAbilitySpecHandle AbilityHandle,
        float Duration);
    PAbilityTaskWaitGameplayEvent* CreateWaitGameplayEventTask(
        FGameplayAbilitySpecHandle AbilityHandle,
        const FGameplayTag& EventTag,
        bool bExactMatch = false);
    PAbilityTaskPlayAnimationAndWait* CreatePlayAnimationAndWaitTask(
        FGameplayAbilitySpecHandle AbilityHandle,
        PAnimInstance* AnimInstance,
        std::shared_ptr<const FAnimationMontageData> Montage,
        std::vector<std::shared_ptr<const FAnimationClipData>> SegmentClips,
        float PlayRate = 1.0f);
    bool CancelAbilityTasks(
        FGameplayAbilitySpecHandle AbilityHandle,
        EAbilityTaskEndReason Reason = EAbilityTaskEndReason::OwnerEnded);
    void TickAbilityTasks(float DeltaSeconds);
    std::vector<PAbilityTask*> GetActiveAbilityTasks() const;
    bool SendGameplayEvent(const FGameplayEventData& EventData);
    TObjectMulticastDelegate<void(const FGameplayEventData&)>& OnGameplayEvent();

    int32 GetGameplayTagCount(const FGameplayTag& Tag) const;
    bool HasMatchingGameplayTag(const FGameplayTag& Tag) const;
    bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& Tags) const;
    bool HasAllMatchingGameplayTags(const FGameplayTagContainer& Tags) const;
    void AddLooseGameplayTag(const FGameplayTag& Tag, int32 Count = 1);
    void RemoveLooseGameplayTag(const FGameplayTag& Tag, int32 Count = 1);
    const FGameplayTagContainer& GetOwnedGameplayTags() const;

    FOnGameplayAbilityEvent& OnAbilityEvent();
    FOnGameplayTagChanged& OnGameplayTagChanged();
    FOnGameplayEffectApplied& OnGameplayEffectApplied();
    FOnGameplayEffectRemoved& OnGameplayEffectRemoved();

    int32 GiveAbilityByClassName(std::string AbilityClassName);
    bool TryActivateAbilityByHandle(int32 Handle);
    bool CancelAbilityByHandle(int32 Handle);
    bool ClearAbilityByHandle(int32 Handle);
    bool SendGameplayEventByTagName(std::string EventTagName, float Magnitude);

protected:
    explicit PGameplayAbilitySystemComponent(const FObjectConstructionParams& Params);
    void PostInitProperties() override;
    void BeginDestroy() override;
    void TickComponent(float DeltaSeconds) override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;

private:
    const PGameplayAbility* ResolveAbilityCDO(const FGameplayAbilitySpec& Spec) const;
    void AddActivationOwnedTags(const PGameplayAbility& Ability);
    void RemoveActivationOwnedTags(const PGameplayAbility& Ability);
    void ChangeTagCount(const FGameplayTag& Tag, int32 Delta);
    void ApplyModifiers(
        const FGameplayEffectSpec& Spec,
        bool bPersistent,
        int32 StackCount,
        FActiveGameplayEffect* ActiveEffect);
    void ApplySingleModifier(
        const FGameplayModifierInfo& Modifier,
        float Level,
        bool bPersistent,
        FActiveGameplayEffect* ActiveEffect);
    void NotifyAbilityTaskEnded(FAbilityTaskHandle Handle);
    void FlushEndedAbilityTasks();

    friend class PAbilityTask;

    TWeakObjectPtr<PActor> AbilityOwnerActor;
    TWeakObjectPtr<PActor> AbilityAvatarActor;
    TObjectPtr<PAttributeSet> AttributeSet;
    std::vector<FGameplayAbilitySpec> ActivatableAbilities;
    std::map<std::string, int32, std::less<>> GameplayTagCounts;
    FGameplayTagContainer OwnedGameplayTags;
    std::vector<FActiveGameplayEffect> ActiveGameplayEffects;
    FOnGameplayAbilityEvent AbilityEvent;
    FOnGameplayTagChanged GameplayTagChangedEvent;
    FOnGameplayEffectApplied GameplayEffectAppliedEvent;
    FOnGameplayEffectRemoved GameplayEffectRemovedEvent;
    TObjectMulticastDelegate<void(const FGameplayEventData&)> GameplayEvent;
    std::vector<FObjectHandle> AbilityTaskHandles;
    std::vector<FAbilityTaskHandle> PendingEndedAbilityTasks;
    uint32 NextAbilityHandle = 1;
    uint32 NextActiveEffectHandle = 1;
    uint32 NextAbilityTaskHandle = 1;
    bool bProcessingActiveGameplayEffects = false;
    std::vector<FActiveGameplayEffectHandle> PendingActiveEffectRemovals;
};
}
