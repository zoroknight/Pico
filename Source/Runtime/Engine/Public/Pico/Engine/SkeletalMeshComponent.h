#pragma once

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/PrimitiveComponent.h"

#include <memory>
#include <array>

namespace Pico
{
class PAnimInstance;
class FReferenceCollector;
enum class EAnimationState : uint8;
enum class EMontageEndReason : uint8;

class PSkeletalMeshComponent final : public PPrimitiveComponent
{
    PICO_DECLARE_CLASS(PSkeletalMeshComponent, PPrimitiveComponent)

public:
    const FAssetPath& GetSkeletalMeshAsset() const;
    const FAssetPath& GetCharacterProfileAsset() const;
    void SetCharacterProfileAsset(const FAssetPath& AssetPath);
    void SetSkeletalMeshAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetMaterialAsset() const;
    void SetMaterialAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetMaterialOverride(std::size_t SlotIndex) const;
    const FAssetPath& GetDefaultMaterial(std::size_t SlotIndex) const;
    std::size_t GetMaterialSlotCount() const;
    void SetMaterialOverride(std::size_t SlotIndex, const FAssetPath& AssetPath);
    const FAssetPath& GetAnimationSetAsset() const;
    void SetAnimationSetAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetDefaultMontageAsset() const;
    void SetDefaultMontageAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetIdleAnimationAsset() const;
    void SetIdleAnimationAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetWalkAnimationAsset() const;
    void SetWalkAnimationAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetJumpAnimationAsset() const;
    void SetJumpAnimationAsset(const FAssetPath& AssetPath);

    void SetRuntimeAnimationSet(
        std::shared_ptr<const FSkeletonData> Skeleton,
        std::shared_ptr<const FSkeletalMeshData> Mesh,
        std::shared_ptr<const FAnimationClipData> Idle,
        std::shared_ptr<const FAnimationClipData> Walk,
        std::shared_ptr<const FAnimationClipData> Jump);
    PAnimInstance* GetAnimInstance() const;
    const FSkinnedMeshRenderData& GetRenderData() const;
    EAnimationState GetAnimationState() const;
    bool SetAnimationPlaybackTime(float TimeSeconds);
    void SetEnableRootMotion(bool bValue);
    bool IsRootMotionEnabled() const;
    bool PlayDefaultMontage(float PlayRate = 1.0f);
    bool StopMontage(EMontageEndReason Reason);

    void TickComponent(float DeltaSeconds) override;
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
    void PostLoad() override;

protected:
    explicit PSkeletalMeshComponent(const FObjectConstructionParams& Params);
    void OnRegister() override;
    void OnUnregister() override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;

private:
    bool LoadConfiguredAssets();
    void InvalidateConfiguredAssets();

    FAssetPath SkeletalMeshAsset;
    FAssetPath CharacterProfileAsset;
    FAssetPath MaterialAsset;
    FAssetPath MaterialOverride0;
    FAssetPath MaterialOverride1;
    FAssetPath MaterialOverride2;
    FAssetPath MaterialOverride3;
    FAssetPath MaterialOverride4;
    FAssetPath MaterialOverride5;
    FAssetPath MaterialOverride6;
    FAssetPath MaterialOverride7;
    FAssetPath AnimationSetAsset;
    FAssetPath DefaultMontageAsset;
    FAssetPath IdleAnimationAsset;
    FAssetPath WalkAnimationAsset;
    FAssetPath JumpAnimationAsset;
    std::shared_ptr<const FSkeletonData> RuntimeSkeleton;
    std::shared_ptr<const FSkeletalMeshData> RuntimeMesh;
    std::shared_ptr<const FAnimationClipData> RuntimeIdle;
    std::shared_ptr<const FAnimationClipData> RuntimeWalk;
    std::shared_ptr<const FAnimationClipData> RuntimeJump;
    std::shared_ptr<const FAnimationSetData> RuntimeAnimationSet;
    std::array<FAssetPath, 8> RuntimeProfileMaterials;
    FSkinnedMeshRenderData RenderData;
    FObjectHandle AnimInstanceHandle;
    bool bEnableRootMotion = false;
};
}
