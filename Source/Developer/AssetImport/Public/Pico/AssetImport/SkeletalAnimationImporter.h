#pragma once

#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/Texture.h"

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
    struct FImportedTexture
    {
        std::string Name;
        FTextureData Texture;
    };
    struct FImportedMaterial
    {
        std::string Name;
        FMaterialData Material;
        int32 BaseColorTextureIndex = -1;
    };

    FSkeletonData Skeleton;
    FSkeletalMeshData Mesh;
    std::vector<FAnimationClipData> Animations;
    std::vector<FImportedTexture> Textures;
    std::vector<FImportedMaterial> Materials;
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
