#pragma once

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/PrimitiveComponent.h"

#include <memory>

namespace Pico
{
class PAnimInstance;
class FReferenceCollector;
enum class EAnimationState : uint8;

class PSkeletalMeshComponent final : public PPrimitiveComponent
{
    PICO_DECLARE_CLASS(PSkeletalMeshComponent, PPrimitiveComponent)

public:
    const FAssetPath& GetSkeletalMeshAsset() const;
    void SetSkeletalMeshAsset(const FAssetPath& AssetPath);
    const FAssetPath& GetMaterialAsset() const;
    void SetMaterialAsset(const FAssetPath& AssetPath);
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

    void TickComponent(float DeltaSeconds) override;

protected:
    explicit PSkeletalMeshComponent(const FObjectConstructionParams& Params);
    void OnRegister() override;
    void OnUnregister() override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;

private:
    bool LoadConfiguredAssets();

    FAssetPath SkeletalMeshAsset;
    FAssetPath MaterialAsset;
    FAssetPath IdleAnimationAsset;
    FAssetPath WalkAnimationAsset;
    FAssetPath JumpAnimationAsset;
    std::shared_ptr<const FSkeletonData> RuntimeSkeleton;
    std::shared_ptr<const FSkeletalMeshData> RuntimeMesh;
    std::shared_ptr<const FAnimationClipData> RuntimeIdle;
    std::shared_ptr<const FAnimationClipData> RuntimeWalk;
    std::shared_ptr<const FAnimationClipData> RuntimeJump;
    FSkinnedMeshRenderData RenderData;
    FObjectHandle AnimInstanceHandle;
    bool bEnableRootMotion = false;
};
}
