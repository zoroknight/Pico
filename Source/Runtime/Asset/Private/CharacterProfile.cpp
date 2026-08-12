#include "Pico/Asset/CharacterProfile.h"

#include "Pico/Core/Config.h"

namespace Pico
{
namespace
{
void Report(ECharacterProfileError* OutError, ECharacterProfileError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

bool HasExtension(const FAssetPath& Path, std::string_view Extension)
{
    return !Path.IsValid() || Path.GetExtension() == Extension;
}

bool ParseOptional(const std::string& Text, FAssetPath& OutPath)
{
    if (Text.empty())
    {
        OutPath = {};
        return true;
    }
    return FAssetPath::TryParse(Text, OutPath);
}
}

bool ValidateCharacterProfile(
    const FCharacterProfileData& Profile,
    ECharacterProfileError* OutError)
{
    Report(OutError, ECharacterProfileError::None);
    if (!Profile.SkeletalMesh.IsValid()
        || !HasExtension(Profile.SkeletalMesh, ".pskeletalmesh")
        || !HasExtension(Profile.AnimationSet, ".panimset")
        || !HasExtension(Profile.DefaultMontage, ".pmontage"))
    {
        Report(OutError, ECharacterProfileError::InvalidData);
        return false;
    }
    for (const FAssetPath& Material : Profile.MaterialOverrides)
    {
        if (!HasExtension(Material, ".pmat"))
        {
            Report(OutError, ECharacterProfileError::InvalidData);
            return false;
        }
    }
    return true;
}

bool SaveCharacterProfileToFile(
    const std::filesystem::path& FilePath,
    const FCharacterProfileData& Profile,
    ECharacterProfileError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, ECharacterProfileError::InvalidArgument);
        return false;
    }
    if (!ValidateCharacterProfile(Profile, OutError)) return false;
    FConfigFile Config;
    Config.SetString("Character", "SkeletalMesh", std::string(Profile.SkeletalMesh.ToString()));
    Config.SetString("Character", "AnimationSet", std::string(Profile.AnimationSet.ToString()));
    Config.SetString("Character", "DefaultMontage", std::string(Profile.DefaultMontage.ToString()));
    for (std::size_t Index = 0; Index < Profile.MaterialOverrides.size(); ++Index)
    {
        Config.SetString("Materials", "Slot" + std::to_string(Index),
            std::string(Profile.MaterialOverrides[Index].ToString()));
    }
    if (!Config.Save(FilePath))
    {
        Report(OutError, ECharacterProfileError::FileWriteFailed);
        return false;
    }
    Report(OutError, ECharacterProfileError::None);
    return true;
}

bool LoadCharacterProfileFromFile(
    const std::filesystem::path& FilePath,
    FCharacterProfileData& OutProfile,
    ECharacterProfileError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, ECharacterProfileError::InvalidArgument);
        return false;
    }
    FConfigFile Config;
    if (!Config.Load(FilePath))
    {
        Report(OutError, ECharacterProfileError::FileReadFailed);
        return false;
    }
    FCharacterProfileData Profile;
    if (!ParseOptional(Config.GetString("Character", "SkeletalMesh", ""), Profile.SkeletalMesh)
        || !ParseOptional(Config.GetString("Character", "AnimationSet", ""), Profile.AnimationSet)
        || !ParseOptional(Config.GetString("Character", "DefaultMontage", ""), Profile.DefaultMontage))
    {
        Report(OutError, ECharacterProfileError::InvalidData);
        return false;
    }
    for (std::size_t Index = 0; Index < Profile.MaterialOverrides.size(); ++Index)
    {
        if (!ParseOptional(Config.GetString("Materials", "Slot" + std::to_string(Index), ""),
                Profile.MaterialOverrides[Index]))
        {
            Report(OutError, ECharacterProfileError::InvalidData);
            return false;
        }
    }
    if (!ValidateCharacterProfile(Profile, OutError)) return false;
    OutProfile = std::move(Profile);
    return true;
}

std::string_view ToString(ECharacterProfileError Error)
{
    switch (Error)
    {
    case ECharacterProfileError::None: return "None";
    case ECharacterProfileError::InvalidArgument: return "InvalidArgument";
    case ECharacterProfileError::InvalidData: return "InvalidData";
    case ECharacterProfileError::FileReadFailed: return "FileReadFailed";
    case ECharacterProfileError::FileWriteFailed: return "FileWriteFailed";
    }
    return "Unknown";
}
}
