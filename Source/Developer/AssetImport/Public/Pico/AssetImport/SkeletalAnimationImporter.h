#pragma once

#include "Pico/Asset/SkeletalAnimation.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Pico
{
enum class ESkeletalImportError
{
    None,
    AssimpUnavailable,
    InvalidArgument,
    ImportFailed,
    MissingMesh,
    MissingSkeleton,
    InvalidSkeleton,
    InvalidWeights,
    InvalidAnimation,
    SaveFailed
};

struct FSkeletalImportOptions
{
    float SourceUnitToCentimeters = 100.0f;
    bool bConvertYUpToZUp = true;
    uint32 MaxBoneInfluences = 4;
};

struct FSkeletalImportResult
{
    FSkeletonData Skeleton;
    FSkeletalMeshData Mesh;
    std::vector<FAnimationClipData> Animations;
    std::vector<std::string> Warnings;
};

bool ImportSkeletalAnimation(
    const std::filesystem::path& SourceFile,
    const FAssetPath& SkeletonAssetPath,
    const FSkeletalImportOptions& Options,
    FSkeletalImportResult& OutResult,
    ESkeletalImportError* OutError = nullptr);

bool SaveSkeletalImportResult(
    const FSkeletalImportResult& Result,
    const std::filesystem::path& SkeletonFile,
    const std::filesystem::path& MeshFile,
    const std::filesystem::path& AnimationDirectory,
    ESkeletalImportError* OutError = nullptr);

std::string_view ToString(ESkeletalImportError Error);
}
