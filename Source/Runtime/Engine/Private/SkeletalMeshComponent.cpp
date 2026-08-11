#include "Pico/Engine/SkeletalMeshComponent.h"

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
    PICO_ADD_ASSET_PROPERTY(Properties, SkeletalMeshAsset, SkeletalMesh);
    PICO_ADD_ASSET_PROPERTY(Properties, MaterialAsset, Material);
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
void PSkeletalMeshComponent::SetSkeletalMeshAsset(const FAssetPath& AssetPath) { SkeletalMeshAsset = AssetPath; }
const FAssetPath& PSkeletalMeshComponent::GetMaterialAsset() const { return MaterialAsset; }
void PSkeletalMeshComponent::SetMaterialAsset(const FAssetPath& AssetPath) { MaterialAsset = AssetPath; }
const FAssetPath& PSkeletalMeshComponent::GetIdleAnimationAsset() const { return IdleAnimationAsset; }
void PSkeletalMeshComponent::SetIdleAnimationAsset(const FAssetPath& AssetPath) { IdleAnimationAsset = AssetPath; }
const FAssetPath& PSkeletalMeshComponent::GetWalkAnimationAsset() const { return WalkAnimationAsset; }
void PSkeletalMeshComponent::SetWalkAnimationAsset(const FAssetPath& AssetPath) { WalkAnimationAsset = AssetPath; }
const FAssetPath& PSkeletalMeshComponent::GetJumpAnimationAsset() const { return JumpAnimationAsset; }
void PSkeletalMeshComponent::SetJumpAnimationAsset(const FAssetPath& AssetPath) { JumpAnimationAsset = AssetPath; }

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
    if (Registry == nullptr || Manager == nullptr || !SkeletalMeshAsset.IsValid()) return false;

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
    SetRuntimeAnimationSet(
        Skeleton,
        Mesh,
        LoadClip(IdleAnimationAsset),
        LoadClip(WalkAnimationAsset),
        LoadClip(JumpAnimationAsset));
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
