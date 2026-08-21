#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Asset/CharacterProfile.h"

#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ReferenceCollector.h"

#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PSkeletalMeshComponent)

bool PSkeletalMeshComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_ASSET_PROPERTY(Properties, CharacterProfileAsset, CharacterProfile);
    PICO_ADD_PROPERTY(Properties, bUseCharacterProfileVisualTransform);
    PICO_ADD_ASSET_PROPERTY(Properties, SkeletalMeshAsset, SkeletalMesh);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialAsset, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride0, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride1, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride2, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride3, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride4, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride5, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride6, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialOverride7, Material);
    PICO_ADD_ASSET_PROPERTY(Properties, AnimationSetAsset, AnimationSet);
    PICO_ADD_ASSET_PROPERTY(Properties, DefaultMontageAsset, AnimationMontage);
    PICO_ADD_ASSET_PROPERTY(Properties, IdleAnimationAsset, AnimationClip);
    PICO_ADD_ASSET_PROPERTY(Properties, WalkAnimationAsset, AnimationClip);
    PICO_ADD_ASSET_PROPERTY(Properties, JumpAnimationAsset, AnimationClip);
    PICO_ADD_PROPERTY(Properties, bEnableRootMotion);
    return Class.AddProperties(std::move(Properties));
}

PSkeletalMeshComponent::PSkeletalMeshComponent(const FObjectConstructionParams& Params)
    : PPrimitiveComponent(Params)
{
    PrimaryComponentTick.SetCanEverTick(true);
    PrimaryComponentTick.SetTickGroup(ETickGroup::PostPhysics);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

const FAssetPath& PSkeletalMeshComponent::GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }
const FAssetPath& PSkeletalMeshComponent::GetCharacterProfileAsset() const { return CharacterProfileAsset; }
void PSkeletalMeshComponent::SetCharacterProfileAsset(const FAssetPath& AssetPath)
{ CharacterProfileAsset = AssetPath; InvalidateConfiguredAssets(); }
bool PSkeletalMeshComponent::UsesCharacterProfileVisualTransform() const
{ return bUseCharacterProfileVisualTransform; }
void PSkeletalMeshComponent::SetUseCharacterProfileVisualTransform(bool bValue)
{ bUseCharacterProfileVisualTransform = bValue; InvalidateConfiguredAssets(); }
const FTransform& PSkeletalMeshComponent::GetCharacterProfileVisualTransform() const
{ return CharacterProfileVisualTransform; }
FTransform PSkeletalMeshComponent::GetVisualWorldTransform() const
{
    return bHasNetworkSmoothingVisualTransform
        ? NetworkSmoothingVisualTransform
        : CharacterProfileVisualTransform * GetWorldTransform();
}
void PSkeletalMeshComponent::SetNetworkSmoothingVisualTransform(
    const FTransform& Transform)
{
    NetworkSmoothingVisualTransform = Transform;
    bHasNetworkSmoothingVisualTransform = true;
}
void PSkeletalMeshComponent::ClearNetworkSmoothingVisualTransform()
{
    NetworkSmoothingVisualTransform = FTransform::Identity;
    bHasNetworkSmoothingVisualTransform = false;
}
bool PSkeletalMeshComponent::HasNetworkSmoothingVisualTransform() const
{ return bHasNetworkSmoothingVisualTransform; }
void PSkeletalMeshComponent::SetSkeletalMeshAsset(const FAssetPath& AssetPath)
{ SkeletalMeshAsset = AssetPath; InvalidateConfiguredAssets(); }
const FAssetPath& PSkeletalMeshComponent::GetMaterialAsset() const { return MaterialAsset; }
void PSkeletalMeshComponent::SetMaterialAsset(const FAssetPath& AssetPath) { MaterialAsset = AssetPath; }
const FAssetPath& PSkeletalMeshComponent::GetMaterialOverride(std::size_t SlotIndex) const
{
    const FAssetPath* InstanceOverride = nullptr;
    switch (SlotIndex)
    {
    case 0: InstanceOverride = &MaterialOverride0; break;
    case 1: InstanceOverride = &MaterialOverride1; break;
    case 2: InstanceOverride = &MaterialOverride2; break;
    case 3: InstanceOverride = &MaterialOverride3; break;
    case 4: InstanceOverride = &MaterialOverride4; break;
    case 5: InstanceOverride = &MaterialOverride5; break;
    case 6: InstanceOverride = &MaterialOverride6; break;
    case 7: InstanceOverride = &MaterialOverride7; break;
    default:
    {
        static const FAssetPath EmptyPath;
        return EmptyPath;
    }
    }
    if (InstanceOverride->IsValid()) return *InstanceOverride;
    return RuntimeProfileMaterials[SlotIndex];
}
const FAssetPath& PSkeletalMeshComponent::GetDefaultMaterial(std::size_t SlotIndex) const
{
    return RuntimeMesh != nullptr && SlotIndex < RuntimeMesh->DefaultMaterials.size()
        ? RuntimeMesh->DefaultMaterials[SlotIndex] : MaterialAsset;
}
std::size_t PSkeletalMeshComponent::GetMaterialSlotCount() const
{
    return RuntimeMesh != nullptr ? RuntimeMesh->Sections.size() : 0;
}
void PSkeletalMeshComponent::SetMaterialOverride(std::size_t SlotIndex, const FAssetPath& AssetPath)
{
    switch (SlotIndex)
    {
    case 0: MaterialOverride0 = AssetPath; break;
    case 1: MaterialOverride1 = AssetPath; break;
    case 2: MaterialOverride2 = AssetPath; break;
    case 3: MaterialOverride3 = AssetPath; break;
    case 4: MaterialOverride4 = AssetPath; break;
    case 5: MaterialOverride5 = AssetPath; break;
    case 6: MaterialOverride6 = AssetPath; break;
    case 7: MaterialOverride7 = AssetPath; break;
    default: break;
    }
}
const FAssetPath& PSkeletalMeshComponent::GetAnimationSetAsset() const { return AnimationSetAsset; }
void PSkeletalMeshComponent::SetAnimationSetAsset(const FAssetPath& AssetPath)
{ AnimationSetAsset = AssetPath; InvalidateConfiguredAssets(); }
const FAssetPath& PSkeletalMeshComponent::GetDefaultMontageAsset() const { return DefaultMontageAsset; }
void PSkeletalMeshComponent::SetDefaultMontageAsset(const FAssetPath& AssetPath) { DefaultMontageAsset = AssetPath; }
const FAssetPath& PSkeletalMeshComponent::GetIdleAnimationAsset() const { return IdleAnimationAsset; }
void PSkeletalMeshComponent::SetIdleAnimationAsset(const FAssetPath& AssetPath)
{ IdleAnimationAsset = AssetPath; InvalidateConfiguredAssets(); }
const FAssetPath& PSkeletalMeshComponent::GetWalkAnimationAsset() const { return WalkAnimationAsset; }
void PSkeletalMeshComponent::SetWalkAnimationAsset(const FAssetPath& AssetPath)
{ WalkAnimationAsset = AssetPath; InvalidateConfiguredAssets(); }
const FAssetPath& PSkeletalMeshComponent::GetJumpAnimationAsset() const { return JumpAnimationAsset; }
void PSkeletalMeshComponent::SetJumpAnimationAsset(const FAssetPath& AssetPath)
{ JumpAnimationAsset = AssetPath; InvalidateConfiguredAssets(); }

void PSkeletalMeshComponent::InvalidateConfiguredAssets()
{
    RuntimeAnimationSet.reset();
    RuntimeSkeleton.reset();
    RuntimeMesh.reset();
    RuntimeIdle.reset();
    RuntimeWalk.reset();
    RuntimeJump.reset();
    RuntimeProfileMaterials = {};
    CharacterProfileVisualTransform = FTransform::Identity;
    ClearNetworkSmoothingVisualTransform();
    RenderData = {};
}

void PSkeletalMeshComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    PPrimitiveComponent::PostEditChangeProperty(Event);
    InvalidateConfiguredAssets();
    LoadConfiguredAssets();
}

void PSkeletalMeshComponent::PostLoad()
{
    PPrimitiveComponent::PostLoad();
    InvalidateConfiguredAssets();
}

void PSkeletalMeshComponent::SetRuntimeAnimationSet(
    std::shared_ptr<const FSkeletonData> Skeleton,
    std::shared_ptr<const FSkeletalMeshData> Mesh,
    std::shared_ptr<const FAnimationClipData> Idle,
    std::shared_ptr<const FAnimationClipData> Walk,
    std::shared_ptr<const FAnimationClipData> Jump)
{
    RuntimeSkeleton = std::move(Skeleton);
    RuntimeMesh = std::move(Mesh);
    RuntimeIdle = std::move(Idle);
    RuntimeWalk = std::move(Walk);
    RuntimeJump = std::move(Jump);
    if (PAnimInstance* Instance = GetAnimInstance())
    {
        Instance->SetAnimationSet(RuntimeSkeleton, RuntimeIdle, RuntimeWalk, RuntimeJump);
    }
    if (RuntimeSkeleton != nullptr && RuntimeMesh != nullptr)
    {
        FSkeletonPose Pose;
        if (BuildReferencePose(*RuntimeSkeleton, Pose))
        {
            SkinSkeletalMesh(*RuntimeMesh, Pose, RenderData);
        }
    }
}

void PSkeletalMeshComponent::OnRegister()
{
    PPrimitiveComponent::OnRegister();
    PAnimInstance* Instance = GetAnimInstance();
    if (Instance == nullptr)
    {
        Instance = NewObject<PAnimInstance>(this, "AnimInstance", EObjectFlags::Transient);
        if (Instance != nullptr) AnimInstanceHandle = Instance->GetHandle();
    }
    if (Instance != nullptr)
    {
        Instance->SetAnimationSet(RuntimeSkeleton, RuntimeIdle, RuntimeWalk, RuntimeJump);
    }
    LoadConfiguredAssets();
}

bool PSkeletalMeshComponent::LoadConfiguredAssets()
{
    if (RuntimeSkeleton != nullptr && RuntimeMesh != nullptr) return true;
    PWorld* World = GetWorld();
    FAssetRegistry* Registry = World != nullptr ? World->GetAssetRegistry() : nullptr;
    FAssetManager* Manager = World != nullptr ? World->GetAssetManager() : nullptr;
    if (Registry == nullptr || Manager == nullptr) return false;

    if (CharacterProfileAsset.IsValid())
    {
        const auto Profile = Manager->LoadCharacterProfile(CharacterProfileAsset, *Registry);
        if (Profile == nullptr) return false;
        SkeletalMeshAsset = Profile->SkeletalMesh;
        AnimationSetAsset = Profile->AnimationSet;
        DefaultMontageAsset = Profile->DefaultMontage;
        RuntimeProfileMaterials = Profile->MaterialOverrides;
        if (bUseCharacterProfileVisualTransform)
        {
            // Older worlds persisted the profile transform directly on the
            // component. Remove that duplicate before composing the new
            // asset-space visual layer.
            if (GetRelativeTransform().Equals(Profile->MeshTransform))
            {
                SetRelativeTransform(FTransform::Identity);
            }
            CharacterProfileVisualTransform = Profile->MeshTransform;
        }
    }
    if (!SkeletalMeshAsset.IsValid()) return false;

    const std::shared_ptr<const FSkeletalMeshData> Mesh =
        Manager->LoadSkeletalMesh(SkeletalMeshAsset, *Registry);
    if (Mesh == nullptr) return false;
    const std::shared_ptr<const FSkeletonData> Skeleton =
        Manager->LoadSkeleton(Mesh->SkeletonAsset, *Registry);
    if (Skeleton == nullptr) return false;
    const auto LoadClip = [Manager, Registry](const FAssetPath& Path)
    {
        return Path.IsValid() ? Manager->LoadAnimationClip(Path, *Registry) : nullptr;
    };
    RuntimeAnimationSet = AnimationSetAsset.IsValid()
        ? Manager->LoadAnimationSet(AnimationSetAsset, *Registry) : nullptr;
    const FAssetPath& IdlePath = RuntimeAnimationSet != nullptr
        ? RuntimeAnimationSet->IdleAnimation : IdleAnimationAsset;
    const FAssetPath& WalkPath = RuntimeAnimationSet != nullptr
        ? RuntimeAnimationSet->WalkAnimation : WalkAnimationAsset;
    const FAssetPath& JumpPath = RuntimeAnimationSet != nullptr
        ? RuntimeAnimationSet->JumpAnimation : JumpAnimationAsset;
    if (RuntimeAnimationSet != nullptr && RuntimeAnimationSet->SkeletonAsset != Mesh->SkeletonAsset)
        return false;
    SetRuntimeAnimationSet(
        Skeleton,
        Mesh,
        LoadClip(IdlePath),
        LoadClip(WalkPath),
        LoadClip(JumpPath));
    return true;
}

void PSkeletalMeshComponent::OnUnregister()
{
    if (PAnimInstance* Instance = GetAnimInstance())
    {
        DestroyObject(Instance);
    }
    AnimInstanceHandle = {};
    PPrimitiveComponent::OnUnregister();
}

void PSkeletalMeshComponent::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PPrimitiveComponent::AddReferencedObjects(Collector);
    Collector.AddReferencedHandle(AnimInstanceHandle);
}

PAnimInstance* PSkeletalMeshComponent::GetAnimInstance() const
{
    PObject* Object = ResolveObject(AnimInstanceHandle);
    return Object != nullptr && Object->IsA(PAnimInstance::StaticClass())
        ? static_cast<PAnimInstance*>(Object)
        : nullptr;
}

const FSkinnedMeshRenderData& PSkeletalMeshComponent::GetRenderData() const { return RenderData; }
EAnimationState PSkeletalMeshComponent::GetAnimationState() const
{
    const PAnimInstance* Instance = GetAnimInstance();
    return Instance != nullptr ? Instance->GetAnimationState() : EAnimationState::Idle;
}
bool PSkeletalMeshComponent::SetAnimationPlaybackTime(float TimeSeconds)
{
    PAnimInstance* Instance = GetAnimInstance();
    return Instance != nullptr
        && RuntimeMesh != nullptr
        && Instance->SetPlaybackTime(TimeSeconds)
        && SkinSkeletalMesh(*RuntimeMesh, Instance->GetPose(), RenderData);
}
void PSkeletalMeshComponent::SetEnableRootMotion(bool bValue) { bEnableRootMotion = bValue; }
bool PSkeletalMeshComponent::IsRootMotionEnabled() const { return bEnableRootMotion; }

bool PSkeletalMeshComponent::PlayDefaultMontage(float PlayRate)
{
    PWorld* World = GetWorld();
    FAssetRegistry* Registry = World != nullptr ? World->GetAssetRegistry() : nullptr;
    FAssetManager* Manager = World != nullptr ? World->GetAssetManager() : nullptr;
    PAnimInstance* Instance = GetAnimInstance();
    if (Registry == nullptr || Manager == nullptr || Instance == nullptr
        || !DefaultMontageAsset.IsValid()) return false;
    const auto Montage = Manager->LoadAnimationMontage(DefaultMontageAsset, *Registry);
    if (Montage == nullptr) return false;
    std::vector<std::shared_ptr<const FAnimationClipData>> Clips;
    Clips.reserve(Montage->Segments.size());
    for (const FAnimationMontageSegment& Segment : Montage->Segments)
    {
        auto Clip = Manager->LoadAnimationClip(Segment.AnimationAsset, *Registry);
        if (Clip == nullptr) return false;
        Clips.push_back(std::move(Clip));
    }
    return Instance->PlayMontage(Montage, std::move(Clips), PlayRate);
}

bool PSkeletalMeshComponent::StopMontage(EMontageEndReason Reason)
{
    PAnimInstance* Instance = GetAnimInstance();
    return Instance != nullptr && Instance->StopMontage(Reason);
}

void PSkeletalMeshComponent::TickComponent(float DeltaSeconds)
{
    PPrimitiveComponent::TickComponent(DeltaSeconds);
    LoadConfiguredAssets();
    PAnimInstance* Instance = GetAnimInstance();
    if (Instance == nullptr || RuntimeSkeleton == nullptr || RuntimeMesh == nullptr) return;

    float GroundSpeed = 0.0f;
    bool bFalling = false;
    PCharacterMovementComponent* Movement = nullptr;
    if (PActor* Owner = GetOwner(); Owner != nullptr && Owner->IsA(PCharacter::StaticClass()))
    {
        Movement = static_cast<PCharacter*>(Owner)->GetCharacterMovement();
        if (Movement != nullptr)
        {
            const FVector3 Velocity = Movement->GetVelocity();
            GroundSpeed = std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y);
            bFalling = Movement->GetMovementMode() == EMovementMode::Falling;
        }
    }

    Instance->SetExtractRootMotion(bEnableRootMotion);
    Instance->Update(DeltaSeconds, GroundSpeed, bFalling);
    SkinSkeletalMesh(*RuntimeMesh, Instance->GetPose(), RenderData);
    const FTransform RootMotion = Instance->ConsumeExtractedRootMotion();
    if (bEnableRootMotion && Movement != nullptr && !RootMotion.Equals(FTransform::Identity))
    {
        Movement->QueueRootMotion(RootMotion);
    }
}
}
