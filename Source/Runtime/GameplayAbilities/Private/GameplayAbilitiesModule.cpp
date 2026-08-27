#include "Pico/GameplayAbilities/GameplayAbilitiesModule.h"

#include "Pico/GameplayAbilities/AbilitySystemComponent.h"
#include "Pico/GameplayAbilities/AbilityTask.h"
#include "Pico/GameplayAbilities/AttributeSet.h"
#include "Pico/GameplayAbilities/GameplayAbility.h"
#include "Pico/GameplayAbilities/GameplayEffect.h"
#include "Pico/Asset/AssetManager.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Graph/ScriptRuntimeExtensions.h"
#include "Pico/Object/ObjectGlobals.h"

#include <utility>

namespace Pico
{
namespace
{
PGameplayAbilitySystemComponent* FindAbilitySystem(PObject* Self)
{
    if (Self == nullptr || !Self->IsA(PActor::StaticClass())) return nullptr;
    for (PActorComponent* Component : static_cast<PActor*>(Self)->GetComponents())
        if (Component != nullptr
            && Component->IsA(PGameplayAbilitySystemComponent::StaticClass()))
            return static_cast<PGameplayAbilitySystemComponent*>(Component);
    return nullptr;
}

PSkeletalMeshComponent* FindSkeletalMesh(PObject* Self)
{
    if (Self == nullptr || !Self->IsA(PActor::StaticClass())) return nullptr;
    for (PActorComponent* Component : static_cast<PActor*>(Self)->GetComponents())
        if (Component != nullptr && Component->IsA(PSkeletalMeshComponent::StaticClass()))
            return static_cast<PSkeletalMeshComponent*>(Component);
    return nullptr;
}

FScriptLatentCancel MakeTaskCancel(PAbilityTask* Task)
{
    const FObjectHandle Handle = Task != nullptr ? Task->GetHandle() : FObjectHandle {};
    return [Handle]()
    {
        PObject* Object = ResolveObject(Handle);
        if (Object != nullptr && Object->IsA(PAbilityTask::StaticClass()))
            static_cast<PAbilityTask*>(Object)->ExternalCancel();
    };
}

bool RegisterGraphGameplayExtensions()
{
    const bool bActivate = RegisterScriptAbilityActivator(
        [](PObject* Self, int32 Handle)
        {
            PGameplayAbilitySystemComponent* ASC = FindAbilitySystem(Self);
            return ASC != nullptr && ASC->TryActivateAbilityByHandle(Handle);
        });
    const bool bWaitEvent = RegisterScriptLatentHandler(
        EScriptLatentAction::WaitGameplayEvent,
        [](PObject* Self, const FScriptExecutionReport& Request,
           FScriptLatentCompletion Completion, FScriptLatentCancel& OutCancel)
        {
            PGameplayAbilitySystemComponent* ASC = FindAbilitySystem(Self);
            if (ASC == nullptr || Request.AbilityHandle <= 0) return false;
            const FGameplayTag Tag = FGameplayTagsManager::Get().RegisterGameplayTag(
                Request.LatentPayload);
            PAbilityTaskWaitGameplayEvent* Task = ASC->CreateWaitGameplayEventTask(
                {static_cast<uint32>(Request.AbilityHandle)}, Tag, Request.bExactMatch);
            if (Task == nullptr) return false;
            Task->OnTaskEnded().AddLambda(
                [Completion = std::move(Completion)](EAbilityTaskEndReason Reason) mutable
                {
                    Completion(Reason == EAbilityTaskEndReason::Completed,
                        Reason == EAbilityTaskEndReason::Completed
                            ? "Gameplay event received" : "Gameplay event wait ended");
                });
            OutCancel = MakeTaskCancel(Task);
            return Task->ReadyForActivation();
        });
    const bool bMontage = RegisterScriptLatentHandler(
        EScriptLatentAction::PlayMontageAndWait,
        [](PObject* Self, const FScriptExecutionReport& Request,
           FScriptLatentCompletion Completion, FScriptLatentCancel& OutCancel)
        {
            PGameplayAbilitySystemComponent* ASC = FindAbilitySystem(Self);
            PSkeletalMeshComponent* Mesh = FindSkeletalMesh(Self);
            PWorld* World = Mesh != nullptr ? Mesh->GetWorld() : nullptr;
            if (ASC == nullptr || Mesh == nullptr || World == nullptr
                || Request.AbilityHandle <= 0) return false;
            FAssetPath MontagePath;
            if (!FAssetPath::TryParse(Request.LatentPayload, MontagePath)) return false;
            FAssetManager* Manager = World->GetAssetManager();
            FAssetRegistry* Registry = World->GetAssetRegistry();
            PAnimInstance* AnimInstance = Mesh->GetAnimInstance();
            if (Manager == nullptr || Registry == nullptr || AnimInstance == nullptr) return false;
            auto Montage = Manager->LoadAnimationMontage(MontagePath, *Registry);
            if (Montage == nullptr) return false;
            std::vector<std::shared_ptr<const FAnimationClipData>> Clips;
            for (const FAnimationMontageSegment& Segment : Montage->Segments)
            {
                auto Clip = Manager->LoadAnimationClip(Segment.AnimationAsset, *Registry);
                if (Clip == nullptr) return false;
                Clips.push_back(std::move(Clip));
            }
            PAbilityTaskPlayAnimationAndWait* Task = ASC->CreatePlayAnimationAndWaitTask(
                {static_cast<uint32>(Request.AbilityHandle)}, AnimInstance,
                std::move(Montage), std::move(Clips), Request.LatentPlayRate);
            if (Task == nullptr) return false;
            Task->OnTaskEnded().AddLambda(
                [Completion = std::move(Completion)](EAbilityTaskEndReason Reason) mutable
                {
                    Completion(Reason == EAbilityTaskEndReason::Completed,
                        Reason == EAbilityTaskEndReason::Completed
                            ? "Montage completed" : "Montage interrupted");
                });
            OutCancel = MakeTaskCancel(Task);
            return Task->ReadyForActivation();
        });
    return bActivate && bWaitEvent && bMontage;
}
}

bool RegisterGameplayAbilitiesClasses()
{
    return PAttributeSet::RegisterClass()
        && PGameplayEffect::RegisterClass()
        && PGameplayAbility::RegisterClass()
        && PAbilityTask::RegisterClass()
        && PAbilityTaskWaitDelay::RegisterClass()
        && PAbilityTaskWaitGameplayEvent::RegisterClass()
        && PAbilityTaskPlayAnimationAndWait::RegisterClass()
        && PGameplayAbilitySystemComponent::RegisterClass()
        && RegisterGraphGameplayExtensions();
}
}
