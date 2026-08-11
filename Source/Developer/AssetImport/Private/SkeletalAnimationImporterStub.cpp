#include "Pico/AssetImport/SkeletalAnimationImporter.h"

namespace Pico
{
bool ImportSkeletalAnimation(
    const std::filesystem::path&,
    const FAssetPath&,
    const FSkeletalImportOptions&,
    FSkeletalImportResult&,
    ESkeletalImportError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalImportError::AssimpUnavailable;
    return false;
}

bool SaveSkeletalImportResult(
    const FSkeletalImportResult&,
    const std::filesystem::path&,
    const std::filesystem::path&,
    const std::filesystem::path&,
    ESkeletalImportError* OutError)
{
    if (OutError != nullptr) *OutError = ESkeletalImportError::AssimpUnavailable;
    return false;
}

std::string_view ToString(ESkeletalImportError Error)
{
    switch (Error)
    {
    case ESkeletalImportError::None: return "None";
    case ESkeletalImportError::AssimpUnavailable: return "AssimpUnavailable";
    case ESkeletalImportError::InvalidArgument: return "InvalidArgument";
    case ESkeletalImportError::ImportFailed: return "ImportFailed";
    case ESkeletalImportError::MissingMesh: return "MissingMesh";
    case ESkeletalImportError::MissingSkeleton: return "MissingSkeleton";
    case ESkeletalImportError::InvalidSkeleton: return "InvalidSkeleton";
    case ESkeletalImportError::InvalidWeights: return "InvalidWeights";
    case ESkeletalImportError::InvalidAnimation: return "InvalidAnimation";
    case ESkeletalImportError::SaveFailed: return "SaveFailed";
    }
    return "Unknown";
}
}
