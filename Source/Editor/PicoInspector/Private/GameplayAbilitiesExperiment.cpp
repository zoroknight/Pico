#include "GameplayAbilitiesExperiment.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/GameplayAbilities/AbilityTask.h"
#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"
#include "Pico/GameplayAbilities/GameplayTag.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"

#include <imgui.h>

#include <algorithm>
#include <sstream>

namespace Pico
{
namespace
{
const char* GetAbilityEventName(EGameplayAbilityEvent Event)
{
    switch (Event)
    {
    case EGameplayAbilityEvent::Granted: return "Granted";
    case EGameplayAbilityEvent::Activated: return "Activated";
    case EGameplayAbilityEvent::Cancelled: return "Cancelled";
    case EGameplayAbilityEvent::Ended: return "Ended";
    case EGameplayAbilityEvent::Removed: return "Removed";
    }
    return "Unknown";
}

const char* GetTaskStateName(EAbilityTaskState State)
{
    switch (State)
    {
    case EAbilityTaskState::Created: return "Created";
    case EAbilityTaskState::ReadyForActivation: return "Ready";
    case EAbilityTaskState::Active: return "Active";
    case EAbilityTaskState::Finished: return "Finished";
    case EAbilityTaskState::Cancelled: return "Cancelled";
    case EAbilityTaskState::Destroyed: return "Destroyed";
    }
    return "Unknown";
}

const char* GetTaskEndReasonName(EAbilityTaskEndReason Reason)
{
    switch (Reason)
    {
    case EAbilityTaskEndReason::Completed: return "Completed";
    case EAbilityTaskEndReason::Cancelled: return "Cancelled";
    case EAbilityTaskEndReason::OwnerEnded: return "OwnerEnded";
    case EAbilityTaskEndReason::Interrupted: return "Interrupted";
    case EAbilityTaskEndReason::Failed: return "Failed";
    }
    return "Unknown";
}

const char* GetMontageEndReasonName(EMontageEndReason Reason)
{
    switch (Reason)
    {
    case EMontageEndReason::Completed: return "Completed";
    case EMontageEndReason::Interrupted: return "Interrupted";
    case EMontageEndReason::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

void DrawSectionLabel(const char* Label)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(Label);
}
}

FGameplayAbilitiesExperiment::~FGameplayAbilitiesExperiment()
{
    TearDown();
}

const char* FGameplayAbilitiesExperiment::GetName() const
{
    return "GAS Lab";
}

bool FGameplayAbilitiesExperiment::SetUp()
{
    if (GetOwner() != nullptr) return true;
    PActor* Owner = NewObject<PActor>(nullptr, "__InspectorAbilityOwner");
    PGameplayAbilitySystemComponent* AbilitySystem = Owner != nullptr
        ? Owner->CreateComponent<PGameplayAbilitySystemComponent>("AbilitySystem")
        : nullptr;
    if (Owner == nullptr || AbilitySystem == nullptr || !AddToRoot(Owner))
    {
        if (Owner != nullptr) DestroyObjectTree(Owner);
        return false;
    }

    OwnerHandle = Owner->GetHandle();
    AbilitySystemHandle = AbilitySystem->GetHandle();
    PAnimInstance* AnimInstance = NewObject<PAnimInstance>(nullptr, "__InspectorTaskAnim");
    if (AnimInstance == nullptr || !AddToRoot(AnimInstance))
    {
        RemoveFromRoot(Owner);
        DestroyObjectTree(Owner);
        if (AnimInstance != nullptr) DestroyObject(AnimInstance);
        OwnerHandle = {};
        AbilitySystemHandle = {};
        return false;
    }
    AnimInstanceHandle = AnimInstance->GetHandle();

    FAssetPath SkeletonPath;
    FAssetPath ClipPath;
    FAssetPath::TryParse("/Game/Inspector/TaskSkeleton.pskeleton", SkeletonPath);
    FAssetPath::TryParse("/Game/Inspector/TaskAction.panimation", ClipPath);
    auto Skeleton = std::make_shared<FSkeletonData>();
    Skeleton->Bones.push_back({"Root", -1, FTransform::Identity, FMatrix4::Identity});
    auto Clip = std::make_shared<FAnimationClipData>();
    Clip->SkeletonAsset = SkeletonPath;
    Clip->Name = "InspectorTaskAction";
    Clip->Duration = 1.0f;
    Clip->bLooping = false;
    auto Montage = std::make_shared<FAnimationMontageData>();
    Montage->SkeletonAsset = SkeletonPath;
    Montage->BlendInTime = 0.0f;
    Montage->BlendOutTime = 0.0f;
    Montage->Segments = {{ClipPath, 0.0f, 1.0f, 1.0f}};
    Montage->Sections = {{"Action", 0.0f, ""}};
    Montage->Notifies = {{"ActionPoint", 0.25f, 0.25f}};
    TaskSkeleton = Skeleton;
    TaskClip = Clip;
    TaskMontage = Montage;
    AnimInstance->SetAnimationSet(TaskSkeleton, TaskClip, TaskClip, TaskClip);
    AbilitySystem->OnAbilityEvent().AddLambda(
        [this](FGameplayAbilitySpecHandle Handle, EGameplayAbilityEvent Event)
        {
            HandleAbilityEvent(Handle, Event);
        });
    AbilitySystem->OnGameplayEffectApplied().AddLambda(
        [this](const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
        {
            HandleEffectApplied(Spec, Handle);
        });
    AbilitySystem->OnGameplayEffectRemoved().AddLambda(
        [this](const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
        {
            HandleEffectRemoved(Spec, Handle);
        });
    if (PAttributeSet* Attributes = AbilitySystem->GetAttributeSet())
    {
        Attributes->OnAttributeChanged().AddLambda(
            [this](const FGameplayAttributeChangeData& Change)
            {
                HandleAttributeChanged(Change);
            });
    }

    const FGameplayTag ActiveTag =
        FGameplayTagsManager::Get().RegisterGameplayTag("Ability.Active.Inspector");
    if (PGameplayAbility* AbilityCDO = GetMutableDefault<PGameplayAbility>())
    {
        AbilityCDO->AddActivationOwnedTag(ActiveTag);
        AbilityCDO->SetDefaultCost(10.0f);
        AbilityCDO->SetDefaultCooldown(3.0f);
    }
    AddLog("Created rooted Owner, ASC, and reflected AttributeSet");
    return true;
}

void FGameplayAbilitiesExperiment::Draw()
{
    PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
    PAttributeSet* Attributes = AbilitySystem != nullptr
        ? AbilitySystem->GetAttributeSet()
        : nullptr;
    if (ImGui::Button("Reset Lab"))
    {
        Reset();
        AbilitySystem = GetAbilitySystem();
        Attributes = AbilitySystem != nullptr ? AbilitySystem->GetAttributeSet() : nullptr;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("No 3D scene is required");

    DrawSectionLabel("Actor Info and Attributes");
    ImGui::Text("OwnerActor: %s", GetOwner() != nullptr ? GetOwner()->GetPathName().c_str() : "Missing");
    ImGui::Text("AvatarActor: %s", AbilitySystem != nullptr && AbilitySystem->GetAbilityAvatarActor() != nullptr
        ? AbilitySystem->GetAbilityAvatarActor()->GetPathName().c_str() : "Missing");
    ImGui::Text("Health: %.1f / %.1f",
        Attributes != nullptr ? Attributes->GetCurrentValue(EGameplayAttribute::Health) : 0.0f,
        Attributes != nullptr ? Attributes->GetCurrentValue(EGameplayAttribute::MaxHealth) : 0.0f);
    ImGui::Text("Mana: %.1f", Attributes != nullptr
        ? Attributes->GetCurrentValue(EGameplayAttribute::Mana) : 0.0f);
    ImGui::Text("MoveSpeed: %.1f", Attributes != nullptr
        ? Attributes->GetCurrentValue(EGameplayAttribute::MoveSpeed) : 0.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Damage", &Damage);
    ImGui::SameLine();
    if (ImGui::Button("Apply Damage")) ApplyDamage();

    DrawSectionLabel("Gameplay Effects");
    if (ImGui::Button("Apply Regeneration")) ApplyRegeneration();
    ImGui::SameLine();
    if (ImGui::Button("Apply Stun")) ApplyStun();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputFloat("Advance Seconds", &AdvanceSeconds, 0.25f, 1.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Advance Effects")) AdvanceEffects();
    ImGui::SameLine();
    if (ImGui::Button("Remove Selected")) RemoveSelectedEffect();

    if (ImGui::BeginTable(
            "ActiveEffects",
            5,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Handle");
        ImGui::TableSetupColumn("Stack Key");
        ImGui::TableSetupColumn("Stacks");
        ImGui::TableSetupColumn("Remaining");
        ImGui::TableSetupColumn("Period");
        ImGui::TableHeadersRow();
        if (AbilitySystem != nullptr)
        {
            for (const FActiveGameplayEffect& Effect : AbilitySystem->GetActiveGameplayEffects())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string Label = std::to_string(Effect.Handle.Value);
                if (ImGui::Selectable(
                        Label.c_str(),
                        SelectedEffectHandle == Effect.Handle.Value,
                        ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectedEffectHandle = Effect.Handle.Value;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Effect.Spec.StackingKey.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", Effect.StackCount);
                ImGui::TableNextColumn();
                if (Effect.Spec.DurationPolicy == EGameplayEffectDurationPolicy::Infinite)
                    ImGui::TextUnformatted("Infinite");
                else
                    ImGui::Text("%.2f", Effect.RemainingDuration);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", Effect.Spec.Period);
            }
        }
        ImGui::EndTable();
    }

    DrawSectionLabel("Ability Spec Lifecycle");
    if (ImGui::Button("Grant Ability")) GrantAbility();
    ImGui::SameLine();
    if (ImGui::Button("Activate")) ActivateAbility();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) CancelAbility();
    ImGui::SameLine();
    if (ImGui::Button("Remove")) RemoveAbility();

    if (ImGui::BeginTable(
            "AbilitySpecs",
            5,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Handle");
        ImGui::TableSetupColumn("Ability Class");
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("Input");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        if (AbilitySystem != nullptr)
        {
            for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string Label = std::to_string(Spec.Handle.Value);
                if (ImGui::Selectable(
                        Label.c_str(),
                        SelectedAbilityHandle == Spec.Handle.Value,
                        ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectedAbilityHandle = Spec.Handle.Value;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Spec.AbilityClass != nullptr
                    ? Spec.AbilityClass->GetName().ToString().c_str() : "None");
                ImGui::TableNextColumn();
                ImGui::Text("%d", Spec.Level);
                ImGui::TableNextColumn();
                ImGui::Text("%d", Spec.InputId);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Spec.IsActive() ? "Active" : "Inactive");
            }
        }
        ImGui::EndTable();
    }
    ImGui::Text("Owned Tags: %s", AbilitySystem != nullptr
        ? AbilitySystem->GetOwnedGameplayTags().ExportText().c_str() : "");

    DrawSectionLabel("Dash Prediction and Reconciliation");
    ImGui::Text("PredictionKey: %u", PendingPredictionKey.Value);
    ImGui::Text("Status: %s", PredictionStatus.c_str());
    ImGui::Text("Simulated Location: (%.1f, %.1f, %.1f)",
        PredictedLocation.X, PredictedLocation.Y, PredictedLocation.Z);
    ImGui::Text("Simulated Mana: %.1f", PredictedMana);
    if (ImGui::Button("Predict Dash")) BeginDashPrediction();
    ImGui::SameLine();
    ImGui::BeginDisabled(!PendingPredictionKey.IsValid());
    if (ImGui::Button("Server Confirm")) ResolveDashPrediction(true);
    ImGui::SameLine();
    if (ImGui::Button("Server Reject + Rollback")) ResolveDashPrediction(false);
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "Prediction changes immediately; rejection restores the captured snapshot.");

    DrawSectionLabel("Ability Tasks");
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Delay Seconds", &DelaySeconds, 0.25f, 1.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Create Delay")) CreateDelayTask();
    if (ImGui::Button("Wait Event")) CreateWaitEventTask();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Event Magnitude", &EventMagnitude, 0.5f, 1.0f, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Send Event")) SendTaskEvent();
    if (ImGui::Button("Play Animation Task")) CreateAnimationTask();
    ImGui::SameLine();
    if (ImGui::Button("Interrupt Animation")) InterruptAnimationTask();
    ImGui::SameLine();
    if (ImGui::Button("Cancel Selected Task")) CancelSelectedTask();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputFloat("Task Advance Seconds", &AdvanceSeconds, 0.25f, 1.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Advance Tasks")) AdvanceTasks();

    if (ImGui::BeginTable(
            "ActiveTasks",
            5,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Handle");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Ability");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Waiting For");
        ImGui::TableHeadersRow();
        if (AbilitySystem != nullptr)
        {
            for (PAbilityTask* Task : AbilitySystem->GetActiveAbilityTasks())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string Label = std::to_string(Task->GetTaskHandle().Value);
                if (ImGui::Selectable(
                        Label.c_str(),
                        SelectedTaskHandle == Task->GetTaskHandle().Value,
                        ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectedTaskHandle = Task->GetTaskHandle().Value;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Task->GetClass()->GetName().ToString().c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", Task->GetOwningAbilityHandle().Value);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GetTaskStateName(Task->GetTaskState()));
                ImGui::TableNextColumn();
                if (Task->IsA(PAbilityTaskWaitDelay::StaticClass()))
                    ImGui::Text("%.2f s", static_cast<PAbilityTaskWaitDelay*>(Task)->GetRemainingTime());
                else if (Task->IsA(PAbilityTaskWaitGameplayEvent::StaticClass()))
                    ImGui::TextUnformatted(
                        static_cast<PAbilityTaskWaitGameplayEvent*>(Task)->GetEventTag().ToString().data());
                else if (Task->IsA(PAbilityTaskPlayAnimationAndWait::StaticClass()))
                    ImGui::TextUnformatted("Montage + Notify");
            }
        }
        ImGui::EndTable();
    }

    DrawSectionLabel("Event Log");
    if (ImGui::Button("Clear Log")) EventLog.clear();
    ImGui::BeginChild("GasEventLog", ImVec2(0.0f, 180.0f), true);
    for (const std::string& Entry : EventLog) ImGui::TextUnformatted(Entry.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void FGameplayAbilitiesExperiment::Reset()
{
    TearDown();
    EventLog.clear();
    SelectedAbilityHandle = 0;
    SelectedEffectHandle = 0;
    SelectedTaskHandle = 0;
    Damage = 20;
    AdvanceSeconds = 1.0f;
    DelaySeconds = 2.0f;
    EventMagnitude = 1.0f;
    PredictionLedger.Reset();
    PendingPredictionKey = {};
    PredictedLocation = FVector3::ZeroVector;
    PredictedMana = 100.0f;
    PredictionStatus = "Idle";
    SetUp();
}

void FGameplayAbilitiesExperiment::TearDown()
{
    if (PActor* Owner = GetOwner())
    {
        RemoveFromRoot(Owner);
        DestroyObjectTree(Owner);
    }
    if (PAnimInstance* AnimInstance = GetAnimInstance())
    {
        RemoveFromRoot(AnimInstance);
        DestroyObject(AnimInstance);
    }
    OwnerHandle = {};
    AbilitySystemHandle = {};
    AnimInstanceHandle = {};
    SelectedAbilityHandle = 0;
    SelectedEffectHandle = 0;
    SelectedTaskHandle = 0;
    TaskSkeleton.reset();
    TaskClip.reset();
    TaskMontage.reset();
    PredictionLedger.Reset();
    PendingPredictionKey = {};
}

PActor* FGameplayAbilitiesExperiment::GetOwner() const
{
    PObject* Object = ResolveObject(OwnerHandle);
    return Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object) : nullptr;
}

PGameplayAbilitySystemComponent* FGameplayAbilitiesExperiment::GetAbilitySystem() const
{
    PObject* Object = ResolveObject(AbilitySystemHandle);
    return Object != nullptr && Object->IsA(PGameplayAbilitySystemComponent::StaticClass())
        ? static_cast<PGameplayAbilitySystemComponent*>(Object) : nullptr;
}

PAnimInstance* FGameplayAbilitiesExperiment::GetAnimInstance() const
{
    PObject* Object = ResolveObject(AnimInstanceHandle);
    return Object != nullptr && Object->IsA(PAnimInstance::StaticClass())
        ? static_cast<PAnimInstance*>(Object) : nullptr;
}

void FGameplayAbilitiesExperiment::GrantAbility()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        const FGameplayAbilitySpecHandle Handle =
            AbilitySystem->GiveAbility(PGameplayAbility::StaticClass());
        if (Handle.IsValid()) SelectedAbilityHandle = Handle.Value;
    }
}

void FGameplayAbilitiesExperiment::ActivateAbility()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        if (!AbilitySystem->TryActivateAbility({SelectedAbilityHandle}))
            AddLog("Activate rejected: select an inactive granted Ability");
    }
}

void FGameplayAbilitiesExperiment::CancelAbility()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        if (!AbilitySystem->CancelAbility({SelectedAbilityHandle}))
            AddLog("Cancel rejected: select an active cancelable Ability");
    }
}

void FGameplayAbilitiesExperiment::RemoveAbility()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        if (AbilitySystem->ClearAbility({SelectedAbilityHandle})) SelectedAbilityHandle = 0;
        else AddLog("Remove rejected: select a granted Ability");
    }
}

void FGameplayAbilitiesExperiment::ApplyDamage()
{
    PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
    if (AbilitySystem != nullptr)
    {
        FGameplayEffectSpec Spec = AbilitySystem->MakeOutgoingSpec(
            PGameplayEffect::StaticClass(), 1.0f, GetOwner());
        Spec.StackingKey = "Inspector.Damage";
        Spec.Modifiers = {{
            EGameplayAttribute::Health,
            EGameplayModifierOperation::Add,
            -static_cast<float>(std::max(0, Damage))}};
        AbilitySystem->ApplyGameplayEffectSpecToSelf(Spec);
    }
}

void FGameplayAbilitiesExperiment::ApplyRegeneration()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        FGameplayEffectSpec Spec = AbilitySystem->MakeOutgoingSpec(
            PGameplayEffect::StaticClass(), 1.0f, GetOwner());
        Spec.DurationPolicy = EGameplayEffectDurationPolicy::Duration;
        Spec.Duration = 5.0f;
        Spec.Period = 1.0f;
        Spec.StackLimitCount = 3;
        Spec.StackingKey = "Inspector.Regeneration";
        Spec.Modifiers = {{
            EGameplayAttribute::Health,
            EGameplayModifierOperation::Add,
            5.0f}};
        FActiveGameplayEffectHandle Handle;
        if (AbilitySystem->ApplyGameplayEffectSpecToSelf(Spec, &Handle))
            SelectedEffectHandle = Handle.Value;
    }
}

void FGameplayAbilitiesExperiment::ApplyStun()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        FGameplayEffectSpec Spec = AbilitySystem->MakeOutgoingSpec(
            PGameplayEffect::StaticClass(), 1.0f, GetOwner());
        Spec.DurationPolicy = EGameplayEffectDurationPolicy::Duration;
        Spec.Duration = 3.0f;
        Spec.StackingKey = "Inspector.Stun";
        Spec.GrantedTags.AddTag(
            FGameplayTagsManager::Get().RegisterGameplayTag("State.Stunned"));
        FActiveGameplayEffectHandle Handle;
        if (AbilitySystem->ApplyGameplayEffectSpecToSelf(Spec, &Handle))
            SelectedEffectHandle = Handle.Value;
    }
}

void FGameplayAbilitiesExperiment::AdvanceEffects()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        AbilitySystem->TickActiveGameplayEffects(std::max(0.0f, AdvanceSeconds));
    }
}

void FGameplayAbilitiesExperiment::RemoveSelectedEffect()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        if (AbilitySystem->RemoveActiveGameplayEffect({SelectedEffectHandle}))
            SelectedEffectHandle = 0;
        else
            AddLog("Remove Effect rejected: select an active Effect");
    }
}

void FGameplayAbilitiesExperiment::BeginDashPrediction()
{
    if (PendingPredictionKey.IsValid())
    {
        AddLog("Prediction rejected locally: resolve the pending key first");
        return;
    }
    if (PredictedMana < 20.0f)
    {
        PredictionStatus = "Blocked: insufficient Mana";
        AddLog(PredictionStatus);
        return;
    }
    FGameplayAbilityPredictionSnapshot Snapshot;
    Snapshot.Transform.Translation = PredictedLocation;
    Snapshot.Mana = PredictedMana;
    PendingPredictionKey = PredictionLedger.BeginPrediction(Snapshot);
    PredictedLocation.X += 260.0f;
    PredictedMana -= 20.0f;
    PredictionStatus = "Pending server result";
    AddLog("Dash predicted locally with key="
        + std::to_string(PendingPredictionKey.Value));
}

void FGameplayAbilitiesExperiment::ResolveDashPrediction(bool bAccepted)
{
    FGameplayAbilityPredictionSnapshot Rollback;
    const EGameplayPredictionResult Result = PredictionLedger.ResolvePrediction(
        PendingPredictionKey, bAccepted, &Rollback);
    if (Result == EGameplayPredictionResult::Unknown)
    {
        AddLog("Prediction result ignored: key is stale or already resolved");
        return;
    }
    if (!bAccepted)
    {
        PredictedLocation = Rollback.Transform.Translation;
        PredictedMana = Rollback.Mana;
        PredictionStatus = "Rejected: snapshot restored";
    }
    else
    {
        PredictionStatus = "Confirmed: predicted state kept";
    }
    AddLog("Dash prediction " + std::string(bAccepted ? "confirmed" : "rejected"));
    PendingPredictionKey = {};
}

void FGameplayAbilitiesExperiment::CreateDelayTask()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        PAbilityTaskWaitDelay* Task = AbilitySystem->CreateWaitDelayTask(
            {SelectedAbilityHandle}, std::max(0.0f, DelaySeconds));
        if (Task == nullptr)
        {
            AddLog("Create Delay rejected: activate the selected Ability first");
            return;
        }
        SelectedTaskHandle = Task->GetTaskHandle().Value;
        Task->OnFinish().AddLambda([this]() { AddLog("Delay Task: Completed"); });
        Task->OnTaskEnded().AddLambda(
            [this](EAbilityTaskEndReason Reason)
            {
                AddLog("Delay Task ended: "
                    + std::string(GetTaskEndReasonName(Reason)));
            });
        Task->ReadyForActivation();
    }
}

void FGameplayAbilitiesExperiment::CreateWaitEventTask()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        const FGameplayTag Tag =
            FGameplayTagsManager::Get().RegisterGameplayTag("Event.Inspector.Action");
        PAbilityTaskWaitGameplayEvent* Task =
            AbilitySystem->CreateWaitGameplayEventTask(
                {SelectedAbilityHandle}, Tag, false);
        if (Task == nullptr)
        {
            AddLog("Wait Event rejected: activate the selected Ability first");
            return;
        }
        SelectedTaskHandle = Task->GetTaskHandle().Value;
        Task->OnEventReceived().AddLambda(
            [this](const FGameplayEventData& Data)
            {
                std::ostringstream Stream;
                Stream << "GameplayEvent received: " << Data.EventTag.ToString()
                    << " magnitude=" << Data.EventMagnitude;
                AddLog(Stream.str());
            });
        Task->ReadyForActivation();
    }
}

void FGameplayAbilitiesExperiment::SendTaskEvent()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        AbilitySystem->SendGameplayEventByTagName(
            "Event.Inspector.Action.Triggered", EventMagnitude);
    }
}

void FGameplayAbilitiesExperiment::CreateAnimationTask()
{
    PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
    PAnimInstance* AnimInstance = GetAnimInstance();
    if (AbilitySystem == nullptr || AnimInstance == nullptr) return;
    PAbilityTaskPlayAnimationAndWait* Task =
        AbilitySystem->CreatePlayAnimationAndWaitTask(
            {SelectedAbilityHandle}, AnimInstance, TaskMontage, {TaskClip});
    if (Task == nullptr)
    {
        AddLog("Animation Task rejected: activate the selected Ability first");
        return;
    }
    SelectedTaskHandle = Task->GetTaskHandle().Value;
    Task->OnNotify().AddLambda(
        [this](std::string_view Name, EMontageNotifyEvent)
        {
            AddLog("Animation Notify: " + std::string(Name));
        });
    Task->OnAnimationEnded().AddLambda(
        [this](EMontageEndReason Reason)
        {
            AddLog("Animation Task ended: "
                + std::string(GetMontageEndReasonName(Reason)));
        });
    Task->ReadyForActivation();
}

void FGameplayAbilitiesExperiment::InterruptAnimationTask()
{
    PAnimInstance* AnimInstance = GetAnimInstance();
    if (AnimInstance == nullptr
        || !AnimInstance->PlayMontage(TaskMontage, {TaskClip}))
    {
        AddLog("Interrupt Animation: no playable fixture");
        return;
    }
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
        AbilitySystem->TickAbilityTasks(0.0001f);
}

void FGameplayAbilitiesExperiment::CancelSelectedTask()
{
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
    {
        for (PAbilityTask* Task : AbilitySystem->GetActiveAbilityTasks())
        {
            if (Task->GetTaskHandle().Value == SelectedTaskHandle)
            {
                Task->ExternalCancel();
                AbilitySystem->TickAbilityTasks(0.0001f);
                SelectedTaskHandle = 0;
                return;
            }
        }
        AddLog("Cancel Task rejected: select an active Task");
    }
}

void FGameplayAbilitiesExperiment::AdvanceTasks()
{
    const float DeltaSeconds = std::max(0.0f, AdvanceSeconds);
    if (PAnimInstance* AnimInstance = GetAnimInstance())
        AnimInstance->Update(DeltaSeconds, 0.0f, false);
    if (PGameplayAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
        AbilitySystem->TickAbilityTasks(DeltaSeconds);
}

void FGameplayAbilitiesExperiment::HandleAbilityEvent(
    FGameplayAbilitySpecHandle Handle,
    EGameplayAbilityEvent Event)
{
    AddLog("Ability " + std::to_string(Handle.Value) + ": " + GetAbilityEventName(Event));
}

void FGameplayAbilitiesExperiment::HandleEffectApplied(
    const FGameplayEffectSpec& Spec,
    FActiveGameplayEffectHandle Handle)
{
    const std::string Name = Spec.StackingKey.empty() ? "Unnamed" : Spec.StackingKey;
    AddLog("Effect applied: " + Name + " (handle="
        + std::to_string(Handle.Value) + ')');
}

void FGameplayAbilitiesExperiment::HandleEffectRemoved(
    const FGameplayEffectSpec& Spec,
    FActiveGameplayEffectHandle Handle)
{
    const std::string Name = Spec.StackingKey.empty() ? "Unnamed" : Spec.StackingKey;
    AddLog("Effect removed: " + Name + " (handle="
        + std::to_string(Handle.Value) + ')');
}

void FGameplayAbilitiesExperiment::HandleAttributeChanged(
    const FGameplayAttributeChangeData& Change)
{
    std::ostringstream Stream;
    Stream << ToString(Change.Attribute) << ": "
        << Change.OldValue.CurrentValue << " -> " << Change.NewValue.CurrentValue
        << " (source=" << Change.Source.ToString() << ')';
    AddLog(Stream.str());
}

void FGameplayAbilitiesExperiment::AddLog(std::string Message)
{
    EventLog.push_back(std::move(Message));
    if (EventLog.size() > 200) EventLog.erase(EventLog.begin(), EventLog.begin() + 50);
}
}
