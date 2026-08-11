#pragma once

#include "Pico/Asset/StaticMesh.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Math/Matrix4.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Types.h"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
enum class ESkeletalAssetError
{
    None,
    InvalidArgument,
    InvalidData,
    InvalidArchive,
    UnsupportedVersion,
    LimitExceeded,
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileTooLarge,
    TrailingData
};

struct FSkeletonBone
{
    std::string Name;
    int32 ParentIndex = -1;
    FTransform ReferenceLocalPose;
    FMatrix4 InverseBindMatrix = FMatrix4::Identity;
};

struct FSkeletonData
{
    std::vector<FSkeletonBone> Bones;
};

struct FSkeletalMeshVertex
{
    FVector3 Position;
    FVector3 Normal;
    FVector2 TexCoord;
    std::array<uint32, 4> BoneIndices {};
    std::array<float, 4> BoneWeights {1.0f, 0.0f, 0.0f, 0.0f};
};

struct FSkeletalMeshData
{
    FAssetPath SkeletonAsset;
    std::vector<FSkeletalMeshVertex> Vertices;
    std::vector<uint32> Indices;
    std::vector<FStaticMeshSection> Sections;
    FStaticMeshBounds Bounds;
};

struct FVectorKey
{
    float Time = 0.0f;
    FVector3 Value;
};

struct FQuatKey
{
    float Time = 0.0f;
    FQuat Value;
};

struct FBoneAnimationTrack
{
    uint32 BoneIndex = 0;
    std::vector<FVectorKey> TranslationKeys;
    std::vector<FQuatKey> RotationKeys;
    std::vector<FVectorKey> ScaleKeys;
};

struct FAnimationNotifyData
{
    std::string Name;
    float BeginTime = 0.0f;
    float EndTime = 0.0f;
};

struct FAnimationClipData
{
    FAssetPath SkeletonAsset;
    std::string Name;
    float Duration = 0.0f;
    bool bLooping = true;
    int32 RootBoneIndex = -1;
    std::vector<FBoneAnimationTrack> Tracks;
    std::vector<FAnimationNotifyData> Notifies;
};

struct FSkeletonPose
{
    std::vector<FTransform> LocalTransforms;
    std::vector<FTransform> ComponentTransforms;
    std::vector<FMatrix4> SkinningMatrices;
};

struct FSkinnedMeshRenderData
{
    std::vector<FStaticMeshVertex> Vertices;
    std::vector<uint32> Indices;
    std::vector<FStaticMeshSection> Sections;
    FStaticMeshBounds Bounds;
    uint64 Revision = 0;
};

bool ValidateSkeleton(const FSkeletonData& Skeleton, ESkeletalAssetError* OutError = nullptr);
bool ValidateSkeletalMesh(
    const FSkeletalMeshData& Mesh,
    const FSkeletonData* Skeleton = nullptr,
    ESkeletalAssetError* OutError = nullptr);
bool ValidateAnimationClip(
    const FAnimationClipData& Clip,
    const FSkeletonData* Skeleton = nullptr,
    ESkeletalAssetError* OutError = nullptr);

bool SaveSkeletonToFile(const std::filesystem::path& FilePath, const FSkeletonData& Skeleton,
    ESkeletalAssetError* OutError = nullptr);
bool LoadSkeletonFromFile(const std::filesystem::path& FilePath, FSkeletonData& OutSkeleton,
    ESkeletalAssetError* OutError = nullptr);
bool SaveSkeletalMeshToFile(const std::filesystem::path& FilePath, const FSkeletalMeshData& Mesh,
    ESkeletalAssetError* OutError = nullptr);
bool LoadSkeletalMeshFromFile(const std::filesystem::path& FilePath, FSkeletalMeshData& OutMesh,
    ESkeletalAssetError* OutError = nullptr);
bool SaveAnimationClipToFile(const std::filesystem::path& FilePath, const FAnimationClipData& Clip,
    ESkeletalAssetError* OutError = nullptr);
bool LoadAnimationClipFromFile(const std::filesystem::path& FilePath, FAnimationClipData& OutClip,
    ESkeletalAssetError* OutError = nullptr);

bool SampleAnimationClip(
    const FSkeletonData& Skeleton,
    const FAnimationClipData& Clip,
    float Time,
    FSkeletonPose& OutPose);
bool BuildReferencePose(const FSkeletonData& Skeleton, FSkeletonPose& OutPose);
bool RebuildPoseFromLocalTransforms(const FSkeletonData& Skeleton, FSkeletonPose& InOutPose);
bool SkinSkeletalMesh(
    const FSkeletalMeshData& Mesh,
    const FSkeletonPose& Pose,
    FSkinnedMeshRenderData& OutRenderData);
FTransform ExtractRootMotion(
    const FSkeletonData& Skeleton,
    const FAnimationClipData& Clip,
    float PreviousTime,
    float CurrentTime);

std::string_view ToString(ESkeletalAssetError Error);
}
