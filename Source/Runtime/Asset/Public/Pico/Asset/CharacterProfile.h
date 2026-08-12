#pragma once

#include "Pico/Core/AssetPath.h"

#include <array>
#include <filesystem>
#include <string_view>

namespace Pico
{
enum class ECharacterProfileError
{
    None,
    InvalidArgument,
    InvalidData,
    FileReadFailed,
    FileWriteFailed
};

struct FCharacterProfileData
{
    FAssetPath SkeletalMesh;
    FAssetPath AnimationSet;
    FAssetPath DefaultMontage;
    std::array<FAssetPath, 8> MaterialOverrides;
};

bool ValidateCharacterProfile(
    const FCharacterProfileData& Profile,
    ECharacterProfileError* OutError = nullptr);
bool SaveCharacterProfileToFile(
    const std::filesystem::path& FilePath,
    const FCharacterProfileData& Profile,
    ECharacterProfileError* OutError = nullptr);
bool LoadCharacterProfileFromFile(
    const std::filesystem::path& FilePath,
    FCharacterProfileData& OutProfile,
    ECharacterProfileError* OutError = nullptr);
std::string_view ToString(ECharacterProfileError Error);
}
