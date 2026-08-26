#include "Pico/GameplayAbilities/AbilitySystemComponent.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ReferenceCollector.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PGameplayAbilitySystemComponent)

bool PGameplayAbilitySystemComponent::RegisterProperties(PClass& Class)
{
    FPropertyMetadata ReferenceMetadata;
    ReferenceMetadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, AbilityOwnerActor, ReferenceMetadata);
    PICO_ADD_PROPERTY_METADATA(Properties, AbilityAvatarActor, ReferenceMetadata);
    PICO_ADD_PROPERTY_METADATA(Properties, AttributeSet, ReferenceMetadata);
    if (!Class.AddProperties(std::move(Properties))) return false;

    std::vector<PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions,
        GiveAbilityByClassName,
        EFunctionFlags::Callable,
        FName("AbilityClassName"));
    PICO_ADD_FUNCTION(
        Functions,
        TryActivateAbilityByHandle,
        EFunctionFlags::Callable,
        FName("Handle"));
    PICO_ADD_FUNCTION(
        Functions,
        CancelAbilityByHandle,
        EFunctionFlags::Callable,
        FName("Handle"));
    PICO_ADD_FUNCTION(
        Functions,
        ClearAbilityByHandle,
        EFunctionFlags::Callable,
        FName("Handle"));
    PICO_ADD_FUNCTION(
        Functions,
        SendGameplayEventByTagName,
        EFunctionFlags::Callable,
        FName("EventTagName"),
        FName("Magnitude"));
    return Class.AddFunctions(std::move(Functions));
}

PGameplayAbilitySystemComponent::PGameplayAbilitySystemComponent(
    const FObjectConstructionParams& Params)
    : PActorComponent(Params)
{
    PrimaryComponentTick.SetCanEverTick(true);
    PrimaryComponentTick.SetTickEnabled(true);
}

bool PGameplayAbilitySystemComponent::InitAbilityActorInfo(
    PActor* InOwnerActor,
    PActor* InAvatarActor)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::InitAbilityActorInfo")
        || InOwnerActor == nullptr
        || InAvatarActor == nullptr
        || InOwnerActor->IsBeginningDestroy()
        || InAvatarActor->IsBeginningDestroy())
    {
        return false;
    }
    AbilityOwnerActor = InOwnerActor;
    AbilityAvatarActor = InAvatarActor;
    return true;
}

PActor* PGameplayAbilitySystemComponent::GetAbilityOwnerActor() const
{
    return AbilityOwnerActor.Get();
}

PActor* PGameplayAbilitySystemComponent::GetAbilityAvatarActor() const
{
    return AbilityAvatarActor.Get();
}

PAttributeSet* PGameplayAbilitySystemComponent::GetAttributeSet() const
{
    return AttributeSet.Get();
}

FGameplayAbilitySpecHandle PGameplayAbilitySystemComponent::GiveAbility(
    const PClass* AbilityClass,
    int32 Level,
    int32 InputId)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::GiveAbility")
        || AbilityClass == nullptr
        || !AbilityClass->IsChildOf(PGameplayAbility::StaticClass())
        || AbilityClass->GetDefaultObject() == nullptr
        || Level < 1
        || NextAbilityHandle == 0)
    {
        return {};
    }

    const FGameplayAbilitySpecHandle Handle {NextAbilityHandle++};
    ActivatableAbilities.push_back(FGameplayAbilitySpec {
        Handle,
        AbilityClass,
        Level,
        InputId,
        -1.0f,
        -1.0f,
        EGameplayAbilityActivationState::Inactive});
    AbilityEvent.Broadcast(Handle, EGameplayAbilityEvent::Granted);
    return Handle;
}

bool PGameplayAbilitySystemComponent::ConfigureAbilitySpec(
    FGameplayAbilitySpecHandle Handle,
    float CostOverride,
    float CooldownOverride)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::ConfigureAbilitySpec")
        || !std::isfinite(CostOverride)
        || !std::isfinite(CooldownOverride)
        || CostOverride < 0.0f
        || CooldownOverride < 0.0f)
    {
        return false;
    }
    FGameplayAbilitySpec* Spec = FindAbilitySpec(Handle);
    if (Spec == nullptr || Spec->IsActive()) return false;
    Spec->CostOverride = CostOverride;
    Spec->CooldownOverride = CooldownOverride;
    return true;
}

bool PGameplayAbilitySystemComponent::ClearAbility(FGameplayAbilitySpecHandle Handle)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::ClearAbility")) return false;
    const auto Existing = std::find_if(
        ActivatableAbilities.begin(), ActivatableAbilities.end(),
        [Handle](const FGameplayAbilitySpec& Spec) { return Spec.Handle == Handle; });
    if (Existing == ActivatableAbilities.end()) return false;
    if (Existing->IsActive()
        && !CancelAbility(Handle)
        && !EndAbility(Handle, true))
    {
        return false;
    }
    AbilityEvent.Broadcast(Handle, EGameplayAbilityEvent::Removed);
    ActivatableAbilities.erase(std::find_if(
        ActivatableAbilities.begin(), ActivatableAbilities.end(),
        [Handle](const FGameplayAbilitySpec& Spec) { return Spec.Handle == Handle; }));
    return true;
}

bool PGameplayAbilitySystemComponent::TryActivateAbility(FGameplayAbilitySpecHandle Handle)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::TryActivateAbility")) return false;
    FGameplayAbilitySpec* Spec = FindAbilitySpec(Handle);
    if (Spec == nullptr || Spec->IsActive()) return false;
    const PGameplayAbility* Ability = ResolveAbilityCDO(*Spec);
    if (Ability == nullptr || !Ability->CanActivateAbility(*this, *Spec)) return false;

    Spec->ActivationState = EGameplayAbilityActivationState::Active;
    AddActivationOwnedTags(*Ability);
    if (!Ability->ActivateAbility(*this, Handle)
        || !Ability->CommitAbility(*this, Handle))
    {
        CancelAbilityTasks(Handle, EAbilityTaskEndReason::OwnerEnded);
        RemoveActivationOwnedTags(*Ability);
        Spec->ActivationState = EGameplayAbilityActivationState::Inactive;
        return false;
    }
    AbilityEvent.Broadcast(Handle, EGameplayAbilityEvent::Activated);
    return true;
}

bool PGameplayAbilitySystemComponent::CancelAbility(FGameplayAbilitySpecHandle Handle)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::CancelAbility")) return false;
    FGameplayAbilitySpec* Spec = FindAbilitySpec(Handle);
    const PGameplayAbility* Ability = Spec != nullptr ? ResolveAbilityCDO(*Spec) : nullptr;
    if (Spec == nullptr || Ability == nullptr || !Spec->IsActive() || !Ability->IsCancelable())
    {
        return false;
    }
    Ability->CancelAbility(*this, Handle);
    AbilityEvent.Broadcast(Handle, EGameplayAbilityEvent::Cancelled);
    return EndAbility(Handle, true);
}

bool PGameplayAbilitySystemComponent::EndAbility(
    FGameplayAbilitySpecHandle Handle,
    bool bWasCancelled)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::EndAbility")) return false;
    FGameplayAbilitySpec* Spec = FindAbilitySpec(Handle);
    const PGameplayAbility* Ability = Spec != nullptr ? ResolveAbilityCDO(*Spec) : nullptr;
    if (Spec == nullptr || Ability == nullptr || !Spec->IsActive()) return false;

    Ability->EndAbility(*this, Handle, bWasCancelled);
    CancelAbilityTasks(Handle, EAbilityTaskEndReason::OwnerEnded);
    RemoveActivationOwnedTags(*Ability);
    Spec->ActivationState = EGameplayAbilityActivationState::Inactive;
    AbilityEvent.Broadcast(Handle, EGameplayAbilityEvent::Ended);
    return true;
}

void PGameplayAbilitySystemComponent::CancelAllAbilities()
{
    std::vector<FGameplayAbilitySpecHandle> ActiveHandles;
    for (const FGameplayAbilitySpec& Spec : ActivatableAbilities)
    {
        if (Spec.IsActive()) ActiveHandles.push_back(Spec.Handle);
    }
    for (const FGameplayAbilitySpecHandle Handle : ActiveHandles)
    {
        if (!CancelAbility(Handle)) EndAbility(Handle, true);
    }
}

void PGameplayAbilitySystemComponent::ClearAllAbilities()
{
    CancelAllAbilities();
    while (!ActivatableAbilities.empty())
    {
        const FGameplayAbilitySpecHandle Handle = ActivatableAbilities.back().Handle;
        AbilityEvent.Broadcast(Handle, EGameplayAbilityEvent::Removed);
        ActivatableAbilities.pop_back();
    }
}

FGameplayAbilitySpec* PGameplayAbilitySystemComponent::FindAbilitySpec(
    FGameplayAbilitySpecHandle Handle)
{
    const auto Existing = std::find_if(
        ActivatableAbilities.begin(), ActivatableAbilities.end(),
        [Handle](const FGameplayAbilitySpec& Spec) { return Spec.Handle == Handle; });
    return Existing != ActivatableAbilities.end() ? &*Existing : nullptr;
}

const FGameplayAbilitySpec* PGameplayAbilitySystemComponent::FindAbilitySpec(
    FGameplayAbilitySpecHandle Handle) const
{
    const auto Existing = std::find_if(
        ActivatableAbilities.begin(), ActivatableAbilities.end(),
        [Handle](const FGameplayAbilitySpec& Spec) { return Spec.Handle == Handle; });
    return Existing != ActivatableAbilities.end() ? &*Existing : nullptr;
}

const std::vector<FGameplayAbilitySpec>&
PGameplayAbilitySystemComponent::GetActivatableAbilities() const
{
    return ActivatableAbilities;
}

FGameplayEffectSpec PGameplayAbilitySystemComponent::MakeOutgoingSpec(
    const PClass* EffectClass,
    float Level,
    PObject* SourceObject) const
{
    if (EffectClass == nullptr
        || !EffectClass->IsChildOf(PGameplayEffect::StaticClass())
        || !std::isfinite(Level)
        || Level <= 0.0f)
    {
        return {};
    }
    const PGameplayEffect* Effect =
        static_cast<const PGameplayEffect*>(EffectClass->GetDefaultObject());
    if (Effect == nullptr) return {};

    FGameplayEffectSpec Spec;
    Spec.EffectClass = EffectClass;
    Spec.SourceHandle = SourceObject != nullptr ? SourceObject->GetHandle() : FObjectHandle {};
    Spec.Level = Level;
    Spec.DurationPolicy = Effect->GetDurationPolicy();
    Spec.Duration = Effect->GetDuration();
    Spec.Period = Effect->GetPeriod();
    Spec.StackLimitCount = Effect->GetStackLimitCount();
    Spec.StackingKey = EffectClass->GetName().ToString();
    Spec.Modifiers = Effect->GetModifiers();
    Spec.ApplicationRequiredTags = Effect->GetApplicationRequiredTags();
    Spec.ApplicationBlockedTags = Effect->GetApplicationBlockedTags();
    Spec.GrantedTags = Effect->GetGrantedTags();
    return Spec;
}

bool PGameplayAbilitySystemComponent::ApplyGameplayEffectSpecToSelf(
    const FGameplayEffectSpec& Spec,
    FActiveGameplayEffectHandle* OutHandle)
{
    if (OutHandle != nullptr) OutHandle->Reset();
    if (!CheckGameThread("PGameplayAbilitySystemComponent::ApplyGameplayEffectSpecToSelf")
        || GetAttributeSet() == nullptr
        || IsBeginningDestroy()
        || bProcessingActiveGameplayEffects
        || !Spec.IsValid()
        || !HasAllMatchingGameplayTags(Spec.ApplicationRequiredTags)
        || HasAnyMatchingGameplayTags(Spec.ApplicationBlockedTags))
    {
        return false;
    }

    if (Spec.DurationPolicy == EGameplayEffectDurationPolicy::Instant)
    {
        ApplyModifiers(Spec, false, 1, nullptr);
        GameplayEffectAppliedEvent.Broadcast(Spec, {});
        return true;
    }

    const std::string StackingKey = Spec.StackingKey.empty()
        ? Spec.EffectClass->GetName().ToString()
        : Spec.StackingKey;
    const auto Existing = std::find_if(
        ActiveGameplayEffects.begin(), ActiveGameplayEffects.end(),
        [&StackingKey](const FActiveGameplayEffect& Active)
        {
            return Active.Spec.StackingKey == StackingKey;
        });
    if (Existing != ActiveGameplayEffects.end())
    {
        if (Existing->StackCount < Existing->Spec.StackLimitCount)
        {
            ++Existing->StackCount;
            if (Existing->Spec.Period <= 0.0f)
            {
                ApplyModifiers(Existing->Spec, true, 1, &*Existing);
            }
        }
        if (Existing->Spec.DurationPolicy == EGameplayEffectDurationPolicy::Duration)
        {
            Existing->RemainingDuration = Existing->Spec.Duration;
        }
        if (OutHandle != nullptr) *OutHandle = Existing->Handle;
        GameplayEffectAppliedEvent.Broadcast(Existing->Spec, Existing->Handle);
        return true;
    }

    if (NextActiveEffectHandle == 0) return false;
    FActiveGameplayEffect Active;
    Active.Handle = {NextActiveEffectHandle++};
    Active.Spec = Spec;
    Active.Spec.StackingKey = StackingKey;
    Active.RemainingDuration = Spec.DurationPolicy == EGameplayEffectDurationPolicy::Duration
        ? Spec.Duration : -1.0f;
    for (const FGameplayTag& Tag : Spec.GrantedTags.GetTags()) ChangeTagCount(Tag, 1);
    if (Spec.Period <= 0.0f) ApplyModifiers(Spec, true, 1, &Active);

    ActiveGameplayEffects.push_back(std::move(Active));
    const FActiveGameplayEffect& Added = ActiveGameplayEffects.back();
    if (OutHandle != nullptr) *OutHandle = Added.Handle;
    GameplayEffectAppliedEvent.Broadcast(Added.Spec, Added.Handle);
    return true;
}

bool PGameplayAbilitySystemComponent::RemoveActiveGameplayEffect(
    FActiveGameplayEffectHandle Handle)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::RemoveActiveGameplayEffect")) return false;
    const auto Existing = std::find_if(
        ActiveGameplayEffects.begin(), ActiveGameplayEffects.end(),
        [Handle](const FActiveGameplayEffect& Active) { return Active.Handle == Handle; });
    if (Existing == ActiveGameplayEffects.end()) return false;
    if (bProcessingActiveGameplayEffects)
    {
        if (std::find(
                PendingActiveEffectRemovals.begin(),
                PendingActiveEffectRemovals.end(),
                Handle) == PendingActiveEffectRemovals.end())
        {
            PendingActiveEffectRemovals.push_back(Handle);
        }
        return true;
    }

    bProcessingActiveGameplayEffects = true;
    const FGameplayEffectSpec RemovedSpec = Existing->Spec;
    for (auto Modifier = Existing->PersistentModifiers.rbegin();
        Modifier != Existing->PersistentModifiers.rend(); ++Modifier)
    {
        if (PAttributeSet* Attributes = GetAttributeSet())
        {
            Attributes->ModifyCurrentValue(
                Modifier->Attribute,
                -Modifier->AppliedDelta,
                FName("GameplayEffect.Remove"));
        }
    }
    for (const FGameplayTag& Tag : Existing->Spec.GrantedTags.GetTags())
    {
        ChangeTagCount(Tag, -1);
    }
    const FActiveGameplayEffectHandle RemovedHandle = Existing->Handle;
    ActiveGameplayEffects.erase(Existing);
    bProcessingActiveGameplayEffects = false;
    GameplayEffectRemovedEvent.Broadcast(RemovedSpec, RemovedHandle);
    const std::vector<FActiveGameplayEffectHandle> DeferredRemovals =
        std::move(PendingActiveEffectRemovals);
    PendingActiveEffectRemovals.clear();
    for (const FActiveGameplayEffectHandle DeferredHandle : DeferredRemovals)
    {
        RemoveActiveGameplayEffect(DeferredHandle);
    }
    return true;
}

void PGameplayAbilitySystemComponent::RemoveAllActiveGameplayEffects()
{
    if (bProcessingActiveGameplayEffects)
    {
        for (const FActiveGameplayEffect& Active : ActiveGameplayEffects)
        {
            RemoveActiveGameplayEffect(Active.Handle);
        }
        return;
    }
    while (!ActiveGameplayEffects.empty())
    {
        RemoveActiveGameplayEffect(ActiveGameplayEffects.back().Handle);
    }
}

void PGameplayAbilitySystemComponent::TickActiveGameplayEffects(float DeltaSeconds)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::TickActiveGameplayEffects")
        || !std::isfinite(DeltaSeconds)
        || DeltaSeconds <= 0.0f)
    {
        return;
    }

    bProcessingActiveGameplayEffects = true;
    std::vector<FActiveGameplayEffectHandle> Expired;
    for (FActiveGameplayEffect& Active : ActiveGameplayEffects)
    {
        float EffectiveDelta = DeltaSeconds;
        if (Active.Spec.DurationPolicy == EGameplayEffectDurationPolicy::Duration)
        {
            EffectiveDelta = std::min(EffectiveDelta, Active.RemainingDuration);
        }
        if (Active.Spec.Period > 0.0f)
        {
            Active.PeriodAccumulator += std::max(0.0f, EffectiveDelta);
            int32 Executions = 0;
            while (Active.PeriodAccumulator + 0.000001f >= Active.Spec.Period
                && Executions < 100)
            {
                Active.PeriodAccumulator -= Active.Spec.Period;
                ApplyModifiers(Active.Spec, false, Active.StackCount, nullptr);
                ++Executions;
            }
        }
        if (Active.Spec.DurationPolicy == EGameplayEffectDurationPolicy::Duration)
        {
            Active.RemainingDuration = std::max(0.0f, Active.RemainingDuration - DeltaSeconds);
            if (Active.RemainingDuration <= 0.0f) Expired.push_back(Active.Handle);
        }
    }
    bProcessingActiveGameplayEffects = false;
    for (const FActiveGameplayEffectHandle Handle : PendingActiveEffectRemovals)
    {
        if (std::find(Expired.begin(), Expired.end(), Handle) == Expired.end())
        {
            Expired.push_back(Handle);
        }
    }
    PendingActiveEffectRemovals.clear();
    for (const FActiveGameplayEffectHandle Handle : Expired)
    {
        RemoveActiveGameplayEffect(Handle);
    }
}

const FActiveGameplayEffect* PGameplayAbilitySystemComponent::FindActiveGameplayEffect(
    FActiveGameplayEffectHandle Handle) const
{
    const auto Existing = std::find_if(
        ActiveGameplayEffects.begin(), ActiveGameplayEffects.end(),
        [Handle](const FActiveGameplayEffect& Active) { return Active.Handle == Handle; });
    return Existing != ActiveGameplayEffects.end() ? &*Existing : nullptr;
}

const std::vector<FActiveGameplayEffect>&
PGameplayAbilitySystemComponent::GetActiveGameplayEffects() const
{
    return ActiveGameplayEffects;
}

PAbilityTaskWaitDelay* PGameplayAbilitySystemComponent::CreateWaitDelayTask(
    FGameplayAbilitySpecHandle AbilityHandle,
    float Duration)
{
    const FGameplayAbilitySpec* Spec = FindAbilitySpec(AbilityHandle);
    if (!CheckGameThread("PGameplayAbilitySystemComponent::CreateWaitDelayTask")
        || Spec == nullptr
        || !Spec->IsActive()
        || NextAbilityTaskHandle == 0)
    {
        return nullptr;
    }
    const FAbilityTaskHandle TaskHandle {NextAbilityTaskHandle++};
    PAbilityTaskWaitDelay* Task = NewObject<PAbilityTaskWaitDelay>(
        nullptr,
        "__AbilityTask_WaitDelay_" + std::to_string(TaskHandle.Value),
        EObjectFlags::Transient);
    if (Task == nullptr
        || !Task->Configure(Duration)
        || !Task->InitializeTask(this, AbilityHandle, TaskHandle))
    {
        if (Task != nullptr) DestroyObject(Task);
        return nullptr;
    }
    AbilityTaskHandles.push_back(Task->GetHandle());
    return Task;
}

PAbilityTaskWaitGameplayEvent*
PGameplayAbilitySystemComponent::CreateWaitGameplayEventTask(
    FGameplayAbilitySpecHandle AbilityHandle,
    const FGameplayTag& EventTag,
    bool bExactMatch)
{
    const FGameplayAbilitySpec* Spec = FindAbilitySpec(AbilityHandle);
    if (!CheckGameThread("PGameplayAbilitySystemComponent::CreateWaitGameplayEventTask")
        || Spec == nullptr
        || !Spec->IsActive()
        || NextAbilityTaskHandle == 0)
    {
        return nullptr;
    }
    const FAbilityTaskHandle TaskHandle {NextAbilityTaskHandle++};
    PAbilityTaskWaitGameplayEvent* Task = NewObject<PAbilityTaskWaitGameplayEvent>(
        nullptr,
        "__AbilityTask_WaitEvent_" + std::to_string(TaskHandle.Value),
        EObjectFlags::Transient);
    if (Task == nullptr
        || !Task->Configure(EventTag, bExactMatch)
        || !Task->InitializeTask(this, AbilityHandle, TaskHandle))
    {
        if (Task != nullptr) DestroyObject(Task);
        return nullptr;
    }
    AbilityTaskHandles.push_back(Task->GetHandle());
    return Task;
}

PAbilityTaskPlayAnimationAndWait*
PGameplayAbilitySystemComponent::CreatePlayAnimationAndWaitTask(
    FGameplayAbilitySpecHandle AbilityHandle,
    PAnimInstance* AnimInstance,
    std::shared_ptr<const FAnimationMontageData> Montage,
    std::vector<std::shared_ptr<const FAnimationClipData>> SegmentClips,
    float PlayRate)
{
    const FGameplayAbilitySpec* Spec = FindAbilitySpec(AbilityHandle);
    if (!CheckGameThread("PGameplayAbilitySystemComponent::CreatePlayAnimationAndWaitTask")
        || Spec == nullptr
        || !Spec->IsActive()
        || NextAbilityTaskHandle == 0)
    {
        return nullptr;
    }
    const FAbilityTaskHandle TaskHandle {NextAbilityTaskHandle++};
    PAbilityTaskPlayAnimationAndWait* Task =
        NewObject<PAbilityTaskPlayAnimationAndWait>(
            nullptr,
            "__AbilityTask_PlayAnimation_" + std::to_string(TaskHandle.Value),
            EObjectFlags::Transient);
    if (Task == nullptr
        || !Task->Configure(
            AnimInstance,
            std::move(Montage),
            std::move(SegmentClips),
            PlayRate)
        || !Task->InitializeTask(this, AbilityHandle, TaskHandle))
    {
        if (Task != nullptr) DestroyObject(Task);
        return nullptr;
    }
    AbilityTaskHandles.push_back(Task->GetHandle());
    return Task;
}

bool PGameplayAbilitySystemComponent::CancelAbilityTasks(
    FGameplayAbilitySpecHandle AbilityHandle,
    EAbilityTaskEndReason Reason)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::CancelAbilityTasks")) return false;
    bool bCancelledAny = false;
    const std::vector<FObjectHandle> Snapshot = AbilityTaskHandles;
    for (const FObjectHandle Handle : Snapshot)
    {
        PObject* Object = ResolveObject(Handle);
        PAbilityTask* Task = Object != nullptr && Object->IsA(PAbilityTask::StaticClass())
            ? static_cast<PAbilityTask*>(Object) : nullptr;
        if (Task != nullptr
            && Task->GetOwningAbilityHandle() == AbilityHandle
            && !Task->IsFinished())
        {
            bCancelledAny = Task->FinishTask(Reason) || bCancelledAny;
        }
    }
    FlushEndedAbilityTasks();
    return bCancelledAny;
}

void PGameplayAbilitySystemComponent::TickAbilityTasks(float DeltaSeconds)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::TickAbilityTasks")
        || !std::isfinite(DeltaSeconds)
        || DeltaSeconds <= 0.0f)
    {
        return;
    }
    const std::vector<FObjectHandle> Snapshot = AbilityTaskHandles;
    for (const FObjectHandle Handle : Snapshot)
    {
        PObject* Object = ResolveObject(Handle);
        PAbilityTask* Task = Object != nullptr && Object->IsA(PAbilityTask::StaticClass())
            ? static_cast<PAbilityTask*>(Object) : nullptr;
        if (Task != nullptr && Task->IsActive()) Task->TickTask(DeltaSeconds);
    }
    FlushEndedAbilityTasks();
}

std::vector<PAbilityTask*>
PGameplayAbilitySystemComponent::GetActiveAbilityTasks() const
{
    std::vector<PAbilityTask*> Result;
    Result.reserve(AbilityTaskHandles.size());
    for (const FObjectHandle Handle : AbilityTaskHandles)
    {
        PObject* Object = ResolveObject(Handle);
        PAbilityTask* Task = Object != nullptr && Object->IsA(PAbilityTask::StaticClass())
            ? static_cast<PAbilityTask*>(Object) : nullptr;
        if (Task != nullptr && !Task->IsFinished()) Result.push_back(Task);
    }
    return Result;
}

bool PGameplayAbilitySystemComponent::SendGameplayEvent(
    const FGameplayEventData& EventData)
{
    if (!CheckGameThread("PGameplayAbilitySystemComponent::SendGameplayEvent")
        || !EventData.EventTag.IsValid())
    {
        return false;
    }
    GameplayEvent.Broadcast(EventData);
    FlushEndedAbilityTasks();
    return true;
}

TObjectMulticastDelegate<void(const FGameplayEventData&)>&
PGameplayAbilitySystemComponent::OnGameplayEvent()
{
    return GameplayEvent;
}

void PGameplayAbilitySystemComponent::NotifyAbilityTaskEnded(FAbilityTaskHandle Handle)
{
    if (!Handle.IsValid()
        || std::find(
            PendingEndedAbilityTasks.begin(),
            PendingEndedAbilityTasks.end(),
            Handle) != PendingEndedAbilityTasks.end())
    {
        return;
    }
    PendingEndedAbilityTasks.push_back(Handle);
}

void PGameplayAbilitySystemComponent::FlushEndedAbilityTasks()
{
    const std::vector<FAbilityTaskHandle> Ended = std::move(PendingEndedAbilityTasks);
    PendingEndedAbilityTasks.clear();
    for (const FAbilityTaskHandle TaskHandle : Ended)
    {
        const auto Existing = std::find_if(
            AbilityTaskHandles.begin(), AbilityTaskHandles.end(),
            [TaskHandle](FObjectHandle ObjectHandle)
            {
                PObject* Object = ResolveObject(ObjectHandle);
                const PAbilityTask* Task =
                    Object != nullptr && Object->IsA(PAbilityTask::StaticClass())
                    ? static_cast<const PAbilityTask*>(Object) : nullptr;
                return Task != nullptr && Task->GetTaskHandle() == TaskHandle;
            });
        if (Existing == AbilityTaskHandles.end()) continue;
        PObject* TaskObject = ResolveObject(*Existing);
        AbilityTaskHandles.erase(Existing);
        if (TaskObject != nullptr) DestroyObject(TaskObject);
    }
}

int32 PGameplayAbilitySystemComponent::GetGameplayTagCount(const FGameplayTag& Tag) const
{
    if (!Tag.IsValid()) return 0;
    int32 Count = 0;
    for (const auto& [Name, TagCount] : GameplayTagCounts)
    {
        const FGameplayTag Candidate = FGameplayTagsManager::Get().RequestGameplayTag(Name);
        if (Candidate.MatchesTag(Tag)) Count += TagCount;
    }
    return Count;
}

bool PGameplayAbilitySystemComponent::HasMatchingGameplayTag(const FGameplayTag& Tag) const
{
    return GetGameplayTagCount(Tag) > 0;
}

bool PGameplayAbilitySystemComponent::HasAnyMatchingGameplayTags(
    const FGameplayTagContainer& Tags) const
{
    return std::any_of(
        Tags.GetTags().begin(), Tags.GetTags().end(),
        [this](const FGameplayTag& Tag) { return HasMatchingGameplayTag(Tag); });
}

bool PGameplayAbilitySystemComponent::HasAllMatchingGameplayTags(
    const FGameplayTagContainer& Tags) const
{
    return std::all_of(
        Tags.GetTags().begin(), Tags.GetTags().end(),
        [this](const FGameplayTag& Tag) { return HasMatchingGameplayTag(Tag); });
}

void PGameplayAbilitySystemComponent::AddLooseGameplayTag(
    const FGameplayTag& Tag,
    int32 Count)
{
    if (CheckGameThread("PGameplayAbilitySystemComponent::AddLooseGameplayTag")
        && Count > 0)
    {
        ChangeTagCount(Tag, Count);
    }
}

void PGameplayAbilitySystemComponent::RemoveLooseGameplayTag(
    const FGameplayTag& Tag,
    int32 Count)
{
    if (CheckGameThread("PGameplayAbilitySystemComponent::RemoveLooseGameplayTag")
        && Count > 0)
    {
        ChangeTagCount(Tag, -Count);
    }
}

const FGameplayTagContainer& PGameplayAbilitySystemComponent::GetOwnedGameplayTags() const
{
    return OwnedGameplayTags;
}

FOnGameplayAbilityEvent& PGameplayAbilitySystemComponent::OnAbilityEvent()
{
    return AbilityEvent;
}

FOnGameplayTagChanged& PGameplayAbilitySystemComponent::OnGameplayTagChanged()
{
    return GameplayTagChangedEvent;
}

FOnGameplayEffectApplied& PGameplayAbilitySystemComponent::OnGameplayEffectApplied()
{
    return GameplayEffectAppliedEvent;
}

FOnGameplayEffectRemoved& PGameplayAbilitySystemComponent::OnGameplayEffectRemoved()
{
    return GameplayEffectRemovedEvent;
}

int32 PGameplayAbilitySystemComponent::GiveAbilityByClassName(
    std::string AbilityClassName)
{
    const PClass* AbilityClass = FClassRegistry::FindClass(FName(AbilityClassName));
    const FGameplayAbilitySpecHandle Handle = GiveAbility(AbilityClass);
    return Handle.Value <= static_cast<uint32>(std::numeric_limits<int32>::max())
        ? static_cast<int32>(Handle.Value)
        : 0;
}

bool PGameplayAbilitySystemComponent::TryActivateAbilityByHandle(int32 Handle)
{
    return Handle > 0 && TryActivateAbility({static_cast<uint32>(Handle)});
}

bool PGameplayAbilitySystemComponent::CancelAbilityByHandle(int32 Handle)
{
    return Handle > 0 && CancelAbility({static_cast<uint32>(Handle)});
}

bool PGameplayAbilitySystemComponent::ClearAbilityByHandle(int32 Handle)
{
    return Handle > 0 && ClearAbility({static_cast<uint32>(Handle)});
}

bool PGameplayAbilitySystemComponent::SendGameplayEventByTagName(
    std::string EventTagName,
    float Magnitude)
{
    if (!std::isfinite(Magnitude)) return false;
    FGameplayEventData EventData;
    EventData.EventTag = FGameplayTagsManager::Get().RegisterGameplayTag(EventTagName);
    EventData.EventMagnitude = Magnitude;
    EventData.InstigatorHandle = GetAbilityOwnerActor() != nullptr
        ? GetAbilityOwnerActor()->GetHandle() : FObjectHandle {};
    EventData.TargetHandle = GetAbilityAvatarActor() != nullptr
        ? GetAbilityAvatarActor()->GetHandle() : FObjectHandle {};
    return SendGameplayEvent(EventData);
}

void PGameplayAbilitySystemComponent::PostInitProperties()
{
    PActorComponent::PostInitProperties();
    PActor* Owner = GetOwner();
    if (Owner != nullptr)
    {
        InitAbilityActorInfo(Owner, Owner);
    }
    if (!HasAnyFlags(GetFlags(), EObjectFlags::ClassDefaultObject)
        && AttributeSet.Get() == nullptr)
    {
        const FObjectHandle Handle = GetHandle();
        const std::string Name = "__ASCAttributes_"
            + std::to_string(Handle.Index) + "_" + std::to_string(Handle.Serial);
        AttributeSet = NewObject<PAttributeSet>(nullptr, Name);
    }
}

void PGameplayAbilitySystemComponent::BeginDestroy()
{
    bProcessingActiveGameplayEffects = false;
    PendingActiveEffectRemovals.clear();
    AbilityEvent.Clear();
    GameplayTagChangedEvent.Clear();
    GameplayEffectAppliedEvent.Clear();
    GameplayEffectRemovedEvent.Clear();
    GameplayEvent.Clear();
    for (const FObjectHandle Handle : AbilityTaskHandles)
    {
        if (PObject* Task = ResolveObject(Handle)) DestroyObject(Task);
    }
    AbilityTaskHandles.clear();
    PendingEndedAbilityTasks.clear();
    ClearAllAbilities();
    RemoveAllActiveGameplayEffects();
    GameplayTagCounts.clear();
    OwnedGameplayTags.Reset();
    AbilityOwnerActor.Reset();
    AbilityAvatarActor.Reset();
    if (PAttributeSet* Attributes = AttributeSet.Get();
        Attributes != nullptr && !IsGarbageCollecting())
    {
        DestroyObject(Attributes);
    }
    AttributeSet.Reset();
    PActorComponent::BeginDestroy();
}

void PGameplayAbilitySystemComponent::TickComponent(float DeltaSeconds)
{
    PActorComponent::TickComponent(DeltaSeconds);
    TickActiveGameplayEffects(DeltaSeconds);
    TickAbilityTasks(DeltaSeconds);
}

void PGameplayAbilitySystemComponent::AddReferencedObjects(
    FReferenceCollector& Collector) const
{
    PActorComponent::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(AbilityTaskHandles);
}

const PGameplayAbility* PGameplayAbilitySystemComponent::ResolveAbilityCDO(
    const FGameplayAbilitySpec& Spec) const
{
    if (Spec.AbilityClass == nullptr
        || !Spec.AbilityClass->IsChildOf(PGameplayAbility::StaticClass()))
    {
        return nullptr;
    }
    return static_cast<const PGameplayAbility*>(Spec.AbilityClass->GetDefaultObject());
}

void PGameplayAbilitySystemComponent::AddActivationOwnedTags(
    const PGameplayAbility& Ability)
{
    for (const FGameplayTag& Tag : Ability.GetActivationOwnedTags().GetTags())
    {
        ChangeTagCount(Tag, 1);
    }
}

void PGameplayAbilitySystemComponent::RemoveActivationOwnedTags(
    const PGameplayAbility& Ability)
{
    for (const FGameplayTag& Tag : Ability.GetActivationOwnedTags().GetTags())
    {
        ChangeTagCount(Tag, -1);
    }
}

void PGameplayAbilitySystemComponent::ChangeTagCount(
    const FGameplayTag& Tag,
    int32 Delta)
{
    if (!Tag.IsValid() || Delta == 0) return;
    const std::string Name(Tag.ToString());
    const auto Existing = GameplayTagCounts.find(Name);
    const int32 OldCount = Existing != GameplayTagCounts.end() ? Existing->second : 0;
    const int32 NewCount = std::max(0, OldCount + Delta);
    if (NewCount == OldCount) return;

    if (NewCount == 0)
    {
        GameplayTagCounts.erase(Name);
        OwnedGameplayTags.RemoveTag(Tag);
    }
    else
    {
        GameplayTagCounts[Name] = NewCount;
        OwnedGameplayTags.AddTag(Tag);
    }
    GameplayTagChangedEvent.Broadcast(Tag, NewCount);
}

void PGameplayAbilitySystemComponent::ApplyModifiers(
    const FGameplayEffectSpec& Spec,
    bool bPersistent,
    int32 StackCount,
    FActiveGameplayEffect* ActiveEffect)
{
    for (int32 StackIndex = 0; StackIndex < StackCount; ++StackIndex)
    {
        for (const FGameplayModifierInfo& Modifier : Spec.Modifiers)
        {
            ApplySingleModifier(Modifier, Spec.Level, bPersistent, ActiveEffect);
        }
    }
}

void PGameplayAbilitySystemComponent::ApplySingleModifier(
    const FGameplayModifierInfo& Modifier,
    float Level,
    bool bPersistent,
    FActiveGameplayEffect* ActiveEffect)
{
    PAttributeSet* Attributes = GetAttributeSet();
    if (Attributes == nullptr) return;
    const float OldValue = bPersistent
        ? Attributes->GetCurrentValue(Modifier.Attribute)
        : Attributes->GetBaseValue(Modifier.Attribute);
    float NewValue = OldValue;
    switch (Modifier.Operation)
    {
    case EGameplayModifierOperation::Add:
        NewValue = OldValue + Modifier.Magnitude * Level;
        break;
    case EGameplayModifierOperation::Multiply:
        NewValue = OldValue * Modifier.Magnitude;
        break;
    case EGameplayModifierOperation::Override:
        NewValue = Modifier.Magnitude;
        break;
    }

    const FName Source("GameplayEffect." + std::string(ToString(Modifier.Attribute)));
    const bool bChanged = bPersistent
        ? Attributes->SetCurrentValue(Modifier.Attribute, NewValue, Source)
        : Attributes->SetBaseValue(Modifier.Attribute, NewValue, Source);
    if (bChanged && bPersistent && ActiveEffect != nullptr)
    {
        const float AppliedValue = Attributes->GetCurrentValue(Modifier.Attribute);
        ActiveEffect->PersistentModifiers.push_back(
            {Modifier.Attribute, AppliedValue - OldValue});
    }
}
}
