#include "TestRunner.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/AbilityTask.h"
#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayAbilitiesModule.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/GameplayAbilities/GameplayAbilityPrediction.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"
#include "Pico/GameplayAbilities/GameplayTag.h"
#include "Pico/Object/Archive.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSerialization.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/Property.h"

#include <string>
#include <memory>
#include <vector>

namespace
{
void TestGameplayTags(FTestRunner& Runner)
{
    Pico::FGameplayTagsManager& Manager = Pico::FGameplayTagsManager::Get();
    const Pico::FGameplayTag Dash = Manager.RegisterGameplayTag("Ability.Movement.Dash");
    const Pico::FGameplayTag Movement = Manager.RequestGameplayTag("Ability.Movement");
    const Pico::FGameplayTag Ability = Manager.RequestGameplayTag("Ability");
    const Pico::FGameplayTag Stunned = Manager.RegisterGameplayTag("State.Stunned");

    Runner.Expect(Dash.IsValid() && Movement.IsValid() && Ability.IsValid(),
        "registering a child GameplayTag deterministically registers its parents");
    Runner.Expect(Dash.MatchesTag(Movement) && Dash.MatchesTag(Ability),
        "a child GameplayTag matches its parent hierarchy");
    Runner.Expect(!Movement.MatchesTag(Dash) && !Dash.MatchesTagExact(Movement),
        "parent matching is directional and exact matching remains strict");
    Runner.Expect(!Manager.RegisterGameplayTag("Bad..Tag").IsValid(),
        "invalid GameplayTag names are rejected");

    Pico::FGameplayTagContainer Tags;
    Runner.Expect(Tags.AddTag(Stunned) && Tags.AddTag(Dash) && !Tags.AddTag(Dash),
        "GameplayTagContainer stores each valid tag once");
    Runner.Expect(Tags.HasTag(Movement) && Tags.HasTagExact(Stunned),
        "GameplayTagContainer supports hierarchical and exact queries");
    Runner.Expect(Tags.ExportText() == "Ability.Movement.Dash,State.Stunned",
        "GameplayTag serialization is sorted and deterministic");

    Pico::FGameplayTagContainer Loaded;
    Runner.Expect(
        Pico::FGameplayTagContainer::ImportText(Tags.ExportText(), Loaded)
            && Loaded.ExportText() == Tags.ExportText(),
        "GameplayTagContainer text serialization round-trips");
    Runner.Expect(Loaded.RemoveTag(Stunned) && !Loaded.HasTagExact(Stunned),
        "GameplayTagContainer removes exact tags");
}

void TestAttributesAndSerialization(FTestRunner& Runner)
{
    Pico::PAttributeSet* Attributes =
        Pico::NewObject<Pico::PAttributeSet>(nullptr, "TestAttributes");
    Runner.Expect(Attributes != nullptr, "AttributeSet can be constructed through reflection");
    if (Attributes == nullptr) return;

    Runner.Expect(
        Attributes->GetCurrentValue(Pico::EGameplayAttribute::Health) == 100.0f
            && Attributes->GetCurrentValue(Pico::EGameplayAttribute::MoveSpeed) == 600.0f,
        "AttributeSet starts with Health=100 and MoveSpeed=600");

    int ChangeCount = 0;
    Pico::FGameplayAttributeChangeData LastChange;
    Attributes->OnAttributeChanged().AddLambda(
        [&ChangeCount, &LastChange](const Pico::FGameplayAttributeChangeData& Change)
        {
            ++ChangeCount;
            LastChange = Change;
        });
    Runner.Expect(
        Attributes->SetCurrentValue(
            Pico::EGameplayAttribute::Health, 75.0f, Pico::FName("Damage.Test"))
            && ChangeCount == 1
            && LastChange.OldValue.CurrentValue == 100.0f
            && LastChange.NewValue.CurrentValue == 75.0f
            && LastChange.Source == Pico::FName("Damage.Test"),
        "all runtime attribute writes broadcast old value, new value, and source");
    Runner.Expect(
        Attributes->SetBaseValue(Pico::EGameplayAttribute::MaxHealth, 150.0f)
            && Attributes->SetBaseValue(Pico::EGameplayAttribute::Health, 120.0f)
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Health) == 120.0f
            && Attributes->GetCurrentValue(Pico::EGameplayAttribute::Health) == 95.0f,
        "changing BaseValue preserves the existing CurrentValue modifier offset");
    const int ChangeCountBeforeClamp = ChangeCount;
    Runner.Expect(
        Attributes->SetBaseValue(
            Pico::EGameplayAttribute::MaxHealth, 80.0f, Pico::FName("Clamp.Test"))
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Health) == 80.0f
            && Attributes->GetCurrentValue(Pico::EGameplayAttribute::Health) == 80.0f
            && ChangeCount == ChangeCountBeforeClamp + 2
            && LastChange.Attribute == Pico::EGameplayAttribute::Health
            && LastChange.Source == Pico::FName("Clamp.Test"),
        "lowering MaxHealth clamps Base and Current Health through one complete notification");

    const Pico::PProperty* HealthProperty =
        Pico::PAttributeSet::StaticClass()->FindProperty(Pico::FName("Health"));
    Runner.Expect(
        HealthProperty != nullptr
            && HealthProperty->HasAnyFlags(Pico::EPropertyFlags::Serializable)
            && HealthProperty->HasAnyFlags(Pico::EPropertyFlags::ReadOnly),
        "Attribute values participate in reflection and serialization without bypassing the API");

    Pico::FMemoryWriter Writer;
    Pico::EObjectSerializationError Error = Pico::EObjectSerializationError::None;
    Runner.Expect(Pico::SaveObject(Writer, Attributes, &Error),
        "AttributeSet saves through Pico object serialization");
    Runner.Expect(Pico::DestroyObject(Attributes), "source AttributeSet is destroyed before reload");
    Pico::FMemoryReader Reader(Writer.GetData());
    Pico::PObject* LoadedObject = Pico::LoadObject(Reader, nullptr, &Error);
    Pico::PAttributeSet* Loaded = LoadedObject != nullptr
            && LoadedObject->IsA(Pico::PAttributeSet::StaticClass())
        ? static_cast<Pico::PAttributeSet*>(LoadedObject)
        : nullptr;
    Runner.Expect(
        Loaded != nullptr
            && Loaded->GetBaseValue(Pico::EGameplayAttribute::Health) == 80.0f
            && Loaded->GetCurrentValue(Pico::EGameplayAttribute::MoveSpeed) == 600.0f,
        "serialized AttributeSet restores BaseValue and CurrentValue");
    if (Loaded != nullptr) Pico::DestroyObject(Loaded);
}

void TestGameplayEffects(FTestRunner& Runner)
{
    Pico::PActor* Owner = Pico::NewObject<Pico::PActor>(nullptr, "EffectOwner");
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = Owner != nullptr
        ? Owner->CreateComponent<Pico::PGameplayAbilitySystemComponent>("EffectSystem")
        : nullptr;
    Runner.Expect(Owner != nullptr && AbilitySystem != nullptr && Pico::AddToRoot(Owner),
        "GameplayEffect test graph is rooted");
    if (Owner == nullptr || AbilitySystem == nullptr) return;

    Pico::PAttributeSet* Attributes = AbilitySystem->GetAttributeSet();
    int AppliedCount = 0;
    int RemovedCount = 0;
    int TagChangeCount = 0;
    AbilitySystem->OnGameplayEffectApplied().AddLambda(
        [&AppliedCount](const Pico::FGameplayEffectSpec&, Pico::FActiveGameplayEffectHandle)
        {
            ++AppliedCount;
        });
    AbilitySystem->OnGameplayEffectRemoved().AddLambda(
        [&RemovedCount](const Pico::FGameplayEffectSpec&, Pico::FActiveGameplayEffectHandle)
        {
            ++RemovedCount;
        });
    AbilitySystem->OnGameplayTagChanged().AddLambda(
        [&TagChangeCount](const Pico::FGameplayTag&, Pico::int32)
        {
            ++TagChangeCount;
        });

    Pico::FGameplayEffectSpec Damage = AbilitySystem->MakeOutgoingSpec(
        Pico::PGameplayEffect::StaticClass(), 1.0f, Owner);
    Damage.StackingKey = "Test.Damage";
    Damage.Modifiers = {{
        Pico::EGameplayAttribute::Health,
        Pico::EGameplayModifierOperation::Add,
        -25.0f}};
    Pico::FActiveGameplayEffectHandle InstantHandle;
    Runner.Expect(
        AbilitySystem->ApplyGameplayEffectSpecToSelf(Damage, &InstantHandle)
            && !InstantHandle.IsValid()
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Health) == 75.0f,
        "an Instant damage Effect modifies the base attribute without becoming active");

    const Pico::FGameplayTag Ready =
        Pico::FGameplayTagsManager::Get().RegisterGameplayTag("State.Ready");
    const Pico::FGameplayTag Stunned =
        Pico::FGameplayTagsManager::Get().RegisterGameplayTag("State.Stunned");
    Pico::FGameplayEffectSpec Conditional = Damage;
    Conditional.Modifiers.front().Magnitude = -5.0f;
    Conditional.ApplicationRequiredTags.AddTag(Ready);
    Runner.Expect(!AbilitySystem->ApplyGameplayEffectSpecToSelf(Conditional),
        "an Effect is rejected when an application-required Tag is missing");
    AbilitySystem->AddLooseGameplayTag(Ready);
    Runner.Expect(
        AbilitySystem->ApplyGameplayEffectSpecToSelf(Conditional)
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Health) == 70.0f,
        "an Effect applies after its required Tag is owned");
    Conditional.ApplicationBlockedTags.AddTag(Stunned);
    AbilitySystem->AddLooseGameplayTag(Stunned);
    Runner.Expect(!AbilitySystem->ApplyGameplayEffectSpecToSelf(Conditional),
        "an Effect is rejected while an application-blocking Tag is owned");
    AbilitySystem->RemoveLooseGameplayTag(Stunned);
    AbilitySystem->RemoveLooseGameplayTag(Ready);

    Attributes->SetBaseValue(Pico::EGameplayAttribute::Health, 50.0f);
    Pico::FGameplayEffectSpec Regeneration = AbilitySystem->MakeOutgoingSpec(
        Pico::PGameplayEffect::StaticClass(), 1.0f, Owner);
    Regeneration.DurationPolicy = Pico::EGameplayEffectDurationPolicy::Duration;
    Regeneration.Duration = 3.0f;
    Regeneration.Period = 1.0f;
    Regeneration.StackLimitCount = 3;
    Regeneration.StackingKey = "Test.Regeneration";
    Regeneration.Modifiers = {{
        Pico::EGameplayAttribute::Health,
        Pico::EGameplayModifierOperation::Add,
        5.0f}};
    Pico::FActiveGameplayEffectHandle RegenHandle;
    bool bStacked = true;
    for (int Index = 0; Index < 4; ++Index)
    {
        Pico::FActiveGameplayEffectHandle AppliedHandle;
        bStacked = bStacked
            && AbilitySystem->ApplyGameplayEffectSpecToSelf(Regeneration, &AppliedHandle);
        if (Index == 0)
            RegenHandle = AppliedHandle;
        else
            bStacked = bStacked && AppliedHandle == RegenHandle;
    }
    const Pico::FActiveGameplayEffect* ActiveRegen =
        AbilitySystem->FindActiveGameplayEffect(RegenHandle);
    Runner.Expect(
        bStacked && ActiveRegen != nullptr && ActiveRegen->StackCount == 3,
        "matching Duration Effects share one handle and stop at their stack limit");
    AbilitySystem->TickActiveGameplayEffects(1.0f);
    Runner.Expect(
        Attributes->GetBaseValue(Pico::EGameplayAttribute::Health) == 65.0f,
        "a Periodic Effect executes once per period and scales by stack count");
    AbilitySystem->TickActiveGameplayEffects(2.0f);
    Runner.Expect(
        AbilitySystem->FindActiveGameplayEffect(RegenHandle) == nullptr
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Health) == 95.0f,
        "a Duration Effect executes only within its lifetime and then removes itself");

    Pico::FGameplayEffectSpec Stun = AbilitySystem->MakeOutgoingSpec(
        Pico::PGameplayEffect::StaticClass(), 1.0f, Owner);
    Stun.DurationPolicy = Pico::EGameplayEffectDurationPolicy::Duration;
    Stun.Duration = 2.0f;
    Stun.StackingKey = "Test.Stun";
    Stun.GrantedTags.AddTag(Stunned);
    Pico::FActiveGameplayEffectHandle StunHandle;
    Runner.Expect(
        AbilitySystem->ApplyGameplayEffectSpecToSelf(Stun, &StunHandle)
            && AbilitySystem->HasMatchingGameplayTag(Stunned),
        "an active Effect grants its owned GameplayTags");
    AbilitySystem->TickActiveGameplayEffects(2.0f);
    Runner.Expect(
        !AbilitySystem->HasMatchingGameplayTag(Stunned)
            && AbilitySystem->FindActiveGameplayEffect(StunHandle) == nullptr,
        "expiring an Effect removes its granted GameplayTags");

    Pico::FGameplayEffectSpec InfiniteBuff = AbilitySystem->MakeOutgoingSpec(
        Pico::PGameplayEffect::StaticClass(), 1.0f, Owner);
    InfiniteBuff.DurationPolicy = Pico::EGameplayEffectDurationPolicy::Infinite;
    InfiniteBuff.StackingKey = "Test.InfiniteSpeed";
    InfiniteBuff.Modifiers = {{
        Pico::EGameplayAttribute::MoveSpeed,
        Pico::EGameplayModifierOperation::Add,
        100.0f}};
    Pico::FActiveGameplayEffectHandle BuffHandle;
    Runner.Expect(
        AbilitySystem->ApplyGameplayEffectSpecToSelf(InfiniteBuff, &BuffHandle)
            && Attributes->GetCurrentValue(Pico::EGameplayAttribute::MoveSpeed) == 700.0f,
        "an Infinite non-periodic Effect applies a persistent current-value modifier");
    Runner.Expect(
        AbilitySystem->RemoveActiveGameplayEffect(BuffHandle)
            && Attributes->GetCurrentValue(Pico::EGameplayAttribute::MoveSpeed) == 600.0f,
        "removing a persistent Effect reverses the modifier it applied");

    Pico::FGameplayEffectSpec Reentrant = Regeneration;
    Reentrant.Duration = 5.0f;
    Reentrant.StackLimitCount = 1;
    Reentrant.StackingKey = "Test.ReentrantRemoval";
    Reentrant.Modifiers.front().Magnitude = 1.0f;
    Pico::FActiveGameplayEffectHandle ReentrantHandle;
    bool bRemovalRequested = false;
    Attributes->OnAttributeChanged().AddLambda(
        [&bRemovalRequested, &ReentrantHandle, AbilitySystem](
            const Pico::FGameplayAttributeChangeData& Change)
        {
            if (!bRemovalRequested
                && ReentrantHandle.IsValid()
                && Change.Attribute == Pico::EGameplayAttribute::Health)
            {
                bRemovalRequested = AbilitySystem->RemoveActiveGameplayEffect(ReentrantHandle);
            }
        });
    AbilitySystem->ApplyGameplayEffectSpecToSelf(Reentrant, &ReentrantHandle);
    AbilitySystem->TickActiveGameplayEffects(1.0f);
    Runner.Expect(
        bRemovalRequested
            && AbilitySystem->FindActiveGameplayEffect(ReentrantHandle) == nullptr,
        "an Effect can request removal from an attribute delegate without invalidating Tick");

    Pico::FGameplayEffectSpec Invalid = Damage;
    Invalid.DurationPolicy = static_cast<Pico::EGameplayEffectDurationPolicy>(255);
    Runner.Expect(!AbilitySystem->ApplyGameplayEffectSpecToSelf(Invalid),
        "invalid reflected enum data is rejected at the Effect Spec boundary");
    Runner.Expect(AppliedCount >= 9 && RemovedCount >= 4 && TagChangeCount >= 4,
        "Effect and Tag multicast delegates expose observable lifecycle changes");

    Pico::PGameplayAbility* AbilityCDO = Pico::GetMutableDefault<Pico::PGameplayAbility>();
    AbilityCDO->SetDefaultCost(10.0f);
    AbilityCDO->SetDefaultCooldown(2.0f);
    const Pico::FGameplayAbilitySpecHandle AbilityHandle =
        AbilitySystem->GiveAbility(Pico::PGameplayAbility::StaticClass());
    Runner.Expect(
        AbilitySystem->TryActivateAbility(AbilityHandle)
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Mana) == 90.0f,
        "Ability Commit pays Cost through an Instant GameplayEffect");
    AbilitySystem->CancelAbility(AbilityHandle);
    Runner.Expect(!AbilitySystem->TryActivateAbility(AbilityHandle),
        "Ability activation is blocked while its cooldown Effect owns the cooldown Tag");
    AbilitySystem->TickActiveGameplayEffects(2.0f);
    Runner.Expect(
        AbilitySystem->TryActivateAbility(AbilityHandle)
            && Attributes->GetBaseValue(Pico::EGameplayAttribute::Mana) == 80.0f,
        "Ability can activate and pay Cost again after the cooldown Effect expires");
    AbilitySystem->ClearAbility(AbilityHandle);
    AbilityCDO->SetDefaultCost(0.0f);
    AbilityCDO->SetDefaultCooldown(0.0f);

    Pico::RemoveFromRoot(Owner);
    Pico::DestroyObjectTree(Owner);
}

void TestAbilityTasks(FTestRunner& Runner)
{
    Pico::PActor* Owner = Pico::NewObject<Pico::PActor>(nullptr, "TaskOwner");
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = Owner != nullptr
        ? Owner->CreateComponent<Pico::PGameplayAbilitySystemComponent>("TaskSystem")
        : nullptr;
    Runner.Expect(Owner != nullptr && AbilitySystem != nullptr && Pico::AddToRoot(Owner),
        "AbilityTask test graph is rooted");
    if (Owner == nullptr || AbilitySystem == nullptr) return;

    if (Pico::PGameplayAbility* AbilityCDO =
        Pico::GetMutableDefault<Pico::PGameplayAbility>())
    {
        AbilityCDO->SetDefaultCost(0.0f);
        AbilityCDO->SetDefaultCooldown(0.0f);
    }
    const Pico::FGameplayAbilitySpecHandle AbilityHandle =
        AbilitySystem->GiveAbility(Pico::PGameplayAbility::StaticClass());
    Runner.Expect(AbilitySystem->TryActivateAbility(AbilityHandle),
        "an active Ability can own asynchronous AbilityTasks");

    Pico::PAbilityTaskWaitDelay* Delay =
        AbilitySystem->CreateWaitDelayTask(AbilityHandle, 1.0f);
    int DelayFinishedCount = 0;
    int DelayEndedCount = 0;
    Pico::EAbilityTaskEndReason DelayReason = Pico::EAbilityTaskEndReason::Failed;
    if (Delay != nullptr)
    {
        Delay->OnFinish().AddLambda([&DelayFinishedCount]() { ++DelayFinishedCount; });
        Delay->OnTaskEnded().AddLambda(
            [&DelayEndedCount, &DelayReason](Pico::EAbilityTaskEndReason Reason)
            {
                ++DelayEndedCount;
                DelayReason = Reason;
            });
    }
    const Pico::FObjectHandle DelayObjectHandle =
        Delay != nullptr ? Delay->GetHandle() : Pico::FObjectHandle {};
    Runner.Expect(
        Delay != nullptr
            && Delay->ReadyForActivation()
            && Delay->IsActive()
            && AbilitySystem->GetActiveAbilityTasks().size() == 1,
        "WaitDelay follows Created -> ReadyForActivation -> Active");
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 0,
        "ASC strongly references an active Task while Owner is reachable");
    AbilitySystem->TickAbilityTasks(0.4f);
    Runner.Expect(
        Pico::ResolveObject(DelayObjectHandle) != nullptr
            && DelayFinishedCount == 0,
        "WaitDelay uses accumulated World delta instead of wall-clock time");
    AbilitySystem->TickAbilityTasks(0.6f);
    AbilitySystem->TickAbilityTasks(1.0f);
    Runner.Expect(
        Pico::ResolveObject(DelayObjectHandle) == nullptr
            && DelayFinishedCount == 1
            && DelayEndedCount == 1
            && DelayReason == Pico::EAbilityTaskEndReason::Completed,
        "WaitDelay completes, broadcasts, and cleans itself exactly once");

    const Pico::FGameplayTag HitParent =
        Pico::FGameplayTagsManager::Get().RegisterGameplayTag("Event.Combat.Hit");
    const Pico::FGameplayTag HitCritical =
        Pico::FGameplayTagsManager::Get().RegisterGameplayTag("Event.Combat.Hit.Critical");
    Pico::PAbilityTaskWaitGameplayEvent* Hierarchical =
        AbilitySystem->CreateWaitGameplayEventTask(AbilityHandle, HitParent, false);
    float ReceivedMagnitude = 0.0f;
    if (Hierarchical != nullptr)
    {
        Hierarchical->OnEventReceived().AddLambda(
            [&ReceivedMagnitude](const Pico::FGameplayEventData& Data)
            {
                ReceivedMagnitude = Data.EventMagnitude;
            });
        Hierarchical->ReadyForActivation();
    }
    Pico::FGameplayEventData CriticalEvent;
    CriticalEvent.EventTag = HitCritical;
    CriticalEvent.EventMagnitude = 42.0f;
    CriticalEvent.InstigatorHandle = Owner->GetHandle();
    CriticalEvent.TargetHandle = Owner->GetHandle();
    const Pico::FObjectHandle HierarchicalHandle =
        Hierarchical != nullptr ? Hierarchical->GetHandle() : Pico::FObjectHandle {};
    Runner.Expect(
        AbilitySystem->SendGameplayEvent(CriticalEvent)
            && ReceivedMagnitude == 42.0f
            && Pico::ResolveObject(HierarchicalHandle) == nullptr,
        "WaitGameplayEvent accepts a child Tag and carries event payload data");

    Pico::PAbilityTaskWaitGameplayEvent* Exact =
        AbilitySystem->CreateWaitGameplayEventTask(AbilityHandle, HitParent, true);
    int ExactCount = 0;
    if (Exact != nullptr)
    {
        Exact->OnEventReceived().AddLambda(
            [&ExactCount](const Pico::FGameplayEventData&) { ++ExactCount; });
        Exact->ReadyForActivation();
    }
    AbilitySystem->SendGameplayEvent(CriticalEvent);
    const bool bExactIgnoredChild = Exact != nullptr && Exact->IsActive() && ExactCount == 0;
    CriticalEvent.EventTag = HitParent;
    AbilitySystem->SendGameplayEvent(CriticalEvent);
    Runner.Expect(bExactIgnoredChild && ExactCount == 1,
        "WaitGameplayEvent exact mode ignores child Tags and accepts only an exact Tag");

    Pico::PAnimInstance* AnimInstance =
        Pico::NewObject<Pico::PAnimInstance>(nullptr, "TaskAnimInstance");
    Pico::FAssetPath SkeletonPath;
    Pico::FAssetPath ClipPath;
    Pico::FAssetPath::TryParse("/Game/Task/Skeleton.pskeleton", SkeletonPath);
    Pico::FAssetPath::TryParse("/Game/Task/Action.panimation", ClipPath);
    auto Skeleton = std::make_shared<Pico::FSkeletonData>();
    Skeleton->Bones.push_back({"Root", -1, Pico::FTransform::Identity, Pico::FMatrix4::Identity});
    auto Clip = std::make_shared<Pico::FAnimationClipData>();
    Clip->SkeletonAsset = SkeletonPath;
    Clip->Name = "TaskAction";
    Clip->Duration = 1.0f;
    Clip->bLooping = false;
    auto Montage = std::make_shared<Pico::FAnimationMontageData>();
    Montage->SkeletonAsset = SkeletonPath;
    Montage->BlendInTime = 0.0f;
    Montage->BlendOutTime = 0.0f;
    Montage->Segments = {{ClipPath, 0.0f, 1.0f, 1.0f}};
    Montage->Sections = {{"Action", 0.0f, ""}};
    Montage->Notifies = {{"ActionPoint", 0.25f, 0.25f}};
    if (AnimInstance != nullptr)
    {
        AnimInstance->SetAnimationSet(Skeleton, Clip, Clip, Clip);
        Pico::AddToRoot(AnimInstance);
    }

    Pico::PAbilityTaskPlayAnimationAndWait* AnimationTask =
        AbilitySystem->CreatePlayAnimationAndWaitTask(
            AbilityHandle, AnimInstance, Montage, {Clip});
    int NotifyCount = 0;
    int CompletedCount = 0;
    if (AnimationTask != nullptr)
    {
        AnimationTask->OnNotify().AddLambda(
            [&NotifyCount](std::string_view Name, Pico::EMontageNotifyEvent Event)
            {
                if (Name == "ActionPoint" && Event == Pico::EMontageNotifyEvent::Trigger)
                    ++NotifyCount;
            });
        AnimationTask->OnAnimationEnded().AddLambda(
            [&CompletedCount](Pico::EMontageEndReason Reason)
            {
                if (Reason == Pico::EMontageEndReason::Completed) ++CompletedCount;
            });
        AnimationTask->ReadyForActivation();
    }
    if (AnimInstance != nullptr)
    {
        AnimInstance->Update(0.3f, 0.0f, false);
        AnimInstance->Update(0.8f, 0.0f, false);
    }
    AbilitySystem->TickAbilityTasks(0.01f);
    Runner.Expect(NotifyCount == 1 && CompletedCount == 1,
        "PlayAnimationAndWait forwards Montage Notify and Completed exactly once");

    Pico::PAbilityTaskPlayAnimationAndWait* InterruptedTask =
        AbilitySystem->CreatePlayAnimationAndWaitTask(
            AbilityHandle, AnimInstance, Montage, {Clip});
    int InterruptedCount = 0;
    if (InterruptedTask != nullptr)
    {
        InterruptedTask->OnAnimationEnded().AddLambda(
            [&InterruptedCount](Pico::EMontageEndReason Reason)
            {
                if (Reason == Pico::EMontageEndReason::Interrupted) ++InterruptedCount;
            });
        InterruptedTask->ReadyForActivation();
    }
    if (AnimInstance != nullptr) AnimInstance->PlayMontage(Montage, {Clip});
    AbilitySystem->TickAbilityTasks(0.01f);
    Runner.Expect(InterruptedCount == 1,
        "starting another Montage interrupts PlayAnimationAndWait through the real AnimInstance delegate");
    if (AnimInstance != nullptr) AnimInstance->StopMontage(Pico::EMontageEndReason::Cancelled);

    Pico::PAbilityTaskWaitDelay* CancelledDelay =
        AbilitySystem->CreateWaitDelayTask(AbilityHandle, 10.0f);
    Pico::EAbilityTaskEndReason CancelReason = Pico::EAbilityTaskEndReason::Completed;
    if (CancelledDelay != nullptr)
    {
        CancelledDelay->OnTaskEnded().AddLambda(
            [&CancelReason](Pico::EAbilityTaskEndReason Reason) { CancelReason = Reason; });
        CancelledDelay->ReadyForActivation();
    }
    Runner.Expect(
        AbilitySystem->CancelAbility(AbilityHandle)
            && CancelReason == Pico::EAbilityTaskEndReason::OwnerEnded
            && AbilitySystem->GetActiveAbilityTasks().empty(),
        "ending an Ability cancels and destroys every Task owned by its SpecHandle");

    AbilitySystem->TryActivateAbility(AbilityHandle);
    Pico::PAbilityTaskWaitGameplayEvent* ReentrantEvent =
        AbilitySystem->CreateWaitGameplayEventTask(AbilityHandle, HitParent, false);
    bool bCancelledFromEvent = false;
    if (ReentrantEvent != nullptr)
    {
        ReentrantEvent->OnEventReceived().AddLambda(
            [&bCancelledFromEvent, AbilitySystem, AbilityHandle](const Pico::FGameplayEventData&)
            {
                bCancelledFromEvent = AbilitySystem->CancelAbility(AbilityHandle);
            });
        ReentrantEvent->ReadyForActivation();
    }
    CriticalEvent.EventTag = HitCritical;
    AbilitySystem->SendGameplayEvent(CriticalEvent);
    Runner.Expect(
        bCancelledFromEvent
            && !AbilitySystem->FindAbilitySpec(AbilityHandle)->IsActive()
            && AbilitySystem->GetActiveAbilityTasks().empty(),
        "a GameplayEvent callback can end its Ability without destroying the executing Task early");

    if (AnimInstance != nullptr)
    {
        Pico::RemoveFromRoot(AnimInstance);
        Pico::DestroyObject(AnimInstance);
    }
    Pico::RemoveFromRoot(Owner);
    Pico::DestroyObjectTree(Owner);
}

void TestAbilitySystemAndGc(FTestRunner& Runner)
{
    Pico::PActor* Owner = Pico::NewObject<Pico::PActor>(nullptr, "AbilityOwner");
    Pico::PGameplayAbilitySystemComponent* AbilitySystem = Owner != nullptr
        ? Owner->CreateComponent<Pico::PGameplayAbilitySystemComponent>("AbilitySystem")
        : nullptr;
    Runner.Expect(Owner != nullptr && AbilitySystem != nullptr,
        "an Actor can own an AbilitySystemComponent");
    if (Owner == nullptr || AbilitySystem == nullptr) return;

    Runner.Expect(Pico::AddToRoot(Owner), "ability owner can be rooted for the runtime test");
    Runner.Expect(
        AbilitySystem->GetAbilityOwnerActor() == Owner
            && AbilitySystem->GetAbilityAvatarActor() == Owner
            && AbilitySystem->GetAttributeSet() != nullptr,
        "ASC initializes OwnerActor, AvatarActor, and its strongly referenced AttributeSet");

    const Pico::FObjectHandle OwnerHandle = Owner->GetHandle();
    const Pico::FObjectHandle AscHandle = AbilitySystem->GetHandle();
    const Pico::FObjectHandle AttributesHandle = AbilitySystem->GetAttributeSet()->GetHandle();
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount == 0,
        "rooted Owner keeps ASC and its reflected AttributeSet reference alive");

    const Pico::FGameplayTag ActiveTag =
        Pico::FGameplayTagsManager::Get().RegisterGameplayTag("Ability.Active.Test");
    Pico::PGameplayAbility* AbilityCDO =
        Pico::GetMutableDefault<Pico::PGameplayAbility>();
    Runner.Expect(AbilityCDO != nullptr && AbilityCDO->AddActivationOwnedTag(ActiveTag),
        "Ability CDO stores default activation tag configuration");

    std::vector<Pico::EGameplayAbilityEvent> Events;
    AbilitySystem->OnAbilityEvent().AddLambda(
        [&Events](Pico::FGameplayAbilitySpecHandle, Pico::EGameplayAbilityEvent Event)
        {
            Events.push_back(Event);
        });
    const Pico::FGameplayAbilitySpecHandle First =
        AbilitySystem->GiveAbility(Pico::PGameplayAbility::StaticClass(), 2, 7);
    const Pico::FGameplayAbilitySpecHandle Second =
        AbilitySystem->GiveAbility(Pico::PGameplayAbility::StaticClass());
    Runner.Expect(First.IsValid() && Second.IsValid() && First != Second,
        "each granted Ability receives a stable per-ASC SpecHandle");
    Runner.Expect(
        AbilitySystem->ConfigureAbilitySpec(First, 7.0f, 0.5f)
            && AbilitySystem->FindAbilitySpec(First)->ResolveCost(10.0f) == 7.0f
            && AbilitySystem->FindAbilitySpec(First)->ResolveCooldown(1.0f) == 0.5f
            && AbilitySystem->FindAbilitySpec(Second)->ResolveCost(10.0f) == 10.0f,
        "per-owner AbilitySpec parameters override cost and cooldown without changing other specs or the CDO");
    Runner.Expect(
        AbilitySystem->TryActivateAbility(First)
            && AbilitySystem->FindAbilitySpec(First)->IsActive()
            && AbilitySystem->HasMatchingGameplayTag(ActiveTag),
        "activating an Ability changes Spec state and grants activation-owned Tags");
    Runner.Expect(
        AbilitySystem->CancelAbility(First)
            && !AbilitySystem->FindAbilitySpec(First)->IsActive()
            && !AbilitySystem->HasMatchingGameplayTag(ActiveTag),
        "cancelling an Ability ends only its Spec runtime state and removes owned Tags");
    Runner.Expect(
        AbilitySystem->ClearAbility(First)
            && AbilitySystem->FindAbilitySpec(First) == nullptr
            && AbilitySystem->FindAbilitySpec(Second) != nullptr,
        "removing one Ability does not disturb another granted Spec");
    Runner.Expect(
        AbilitySystem->TryActivateAbility(Second)
            && AbilitySystem->ClearAbility(Second)
            && !AbilitySystem->HasMatchingGameplayTag(ActiveTag),
        "removing an active Ability force-ends its Spec and cleans activation-owned Tags");
    Runner.Expect(
        AbilityCDO->GetActivationOwnedTags().HasTagExact(ActiveTag)
            && Events.size() >= 9,
        "runtime activation leaves Ability CDO defaults unchanged and emits lifecycle events");

    Runner.Expect(Pico::RemoveFromRoot(Owner), "ability owner root is released");
    Runner.Expect(Pico::CollectGarbage().CollectedObjectCount >= 3,
        "unrooted Owner, ASC, and AttributeSet are collected as one gameplay graph");
    Runner.Expect(
        Pico::ResolveObject(OwnerHandle) == nullptr
            && Pico::ResolveObject(AscHandle) == nullptr
            && Pico::ResolveObject(AttributesHandle) == nullptr,
        "Owner destruction leaves no ASC, AttributeSet, Spec, or delegate object alive");
}

void TestGameplayPredictionLedger(FTestRunner& Runner)
{
    Pico::FGameplayPredictionLedger Ledger;
    Pico::FGameplayAbilityPredictionSnapshot FirstSnapshot;
    FirstSnapshot.Transform.Translation = {10.0f, 20.0f, 30.0f};
    FirstSnapshot.Health = 75.0f;
    FirstSnapshot.Mana = 40.0f;
    FirstSnapshot.OwnedTags = "State.Ready";
    const Pico::FGameplayPredictionKey First = Ledger.BeginPrediction(FirstSnapshot);
    const Pico::FGameplayPredictionKey Second = Ledger.BeginPrediction({});
    Runner.Expect(First.IsValid() && Second.IsValid() && First != Second
            && Ledger.NumPendingPredictions() == 2,
        "PredictionLedger issues stable non-zero keys for concurrent predicted abilities");

    Runner.Expect(
        Ledger.ResolvePrediction(First, true) == Pico::EGameplayPredictionResult::Confirmed
            && Ledger.NumPendingPredictions() == 1,
        "an accepted PredictionKey is confirmed exactly once");
    Pico::FGameplayAbilityPredictionSnapshot Rollback;
    Runner.Expect(
        Ledger.ResolvePrediction(Second, false, &Rollback)
                == Pico::EGameplayPredictionResult::Rejected
            && Rollback.Transform.Translation.IsNearlyZero()
            && Ledger.NumPendingPredictions() == 0,
        "a rejected PredictionKey returns its rollback snapshot");
    Runner.Expect(
        Ledger.ResolvePrediction(Second, true) == Pico::EGameplayPredictionResult::Unknown,
        "duplicate or stale PredictionKey results cannot resolve twice");
    Ledger.Reset();
    Runner.Expect(Ledger.FindPrediction(First) == nullptr
            && Ledger.BeginPrediction({}).Value == 1,
        "PredictionLedger clears pending state at World or owner teardown");
}
}

int main()
{
    FTestRunner Runner;
    Runner.Expect(Pico::PObjectSystem::Init(), "Object system initializes");
    if (Pico::PObjectSystem::IsInitialized())
    {
        Runner.Expect(Pico::PActorComponent::RegisterClass(), "ActorComponent class registers");
        Runner.Expect(Pico::PActor::RegisterClass(), "Actor class registers");
        Runner.Expect(Pico::PAnimInstance::RegisterClass(), "AnimInstance class registers");
        Runner.Expect(Pico::RegisterGameplayAbilitiesClasses(),
            "GameplayAbilities reflection classes register");
        TestGameplayTags(Runner);
        TestAttributesAndSerialization(Runner);
        TestGameplayEffects(Runner);
        TestAbilityTasks(Runner);
        TestAbilitySystemAndGc(Runner);
        TestGameplayPredictionLedger(Runner);
        Pico::PObjectSystem::Shutdown();
    }
    return Runner.Finish();
}
