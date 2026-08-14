#include "Pico/Asset/CharacterProfile.h"

#include "Pico/Core/Config.h"

#include <cmath>
#include <sstream>

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

std::string FormatVector(const FVector3& Value)
{
    std::ostringstream Stream;
    Stream << Value.X << ',' << Value.Y << ',' << Value.Z;
    return Stream.str();
}

bool ParseVector(const std::string& Text, const FVector3& Default, FVector3& OutValue)
{
    if (Text.empty())
    {
        OutValue = Default;
        return true;
    }
    std::istringstream Stream(Text);
    char FirstComma = 0;
    char SecondComma = 0;
    FVector3 Value;
    if (!(Stream >> Value.X >> FirstComma >> Value.Y >> SecondComma >> Value.Z)
        || FirstComma != ',' || SecondComma != ',')
    {
        return false;
    }
    Stream >> std::ws;
    if (!Stream.eof()
        || !std::isfinite(Value.X)
        || !std::isfinite(Value.Y)
        || !std::isfinite(Value.Z))
    {
        return false;
    }
    OutValue = Value;
    return true;
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
    const FVector3& Translation = Profile.MeshTransform.Translation;
    const FVector3& Scale = Profile.MeshTransform.Scale;
    const FRotator Rotation = Profile.MeshTransform.Rotation.Rotator();
    if (!std::isfinite(Translation.X) || !std::isfinite(Translation.Y)
        || !std::isfinite(Translation.Z) || !std::isfinite(Rotation.Pitch)
        || !std::isfinite(Rotation.Yaw) || !std::isfinite(Rotation.Roll)
        || !std::isfinite(Scale.X) || !std::isfinite(Scale.Y)
        || !std::isfinite(Scale.Z) || std::abs(Scale.X) <= SmallNumber
        || std::abs(Scale.Y) <= SmallNumber || std::abs(Scale.Z) <= SmallNumber)
    {
        Report(OutError, ECharacterProfileError::InvalidData);
        return false;
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
    Config.SetString("Visual", "MeshLocation", FormatVector(Profile.MeshTransform.Translation));
    const FRotator MeshRotation = Profile.MeshTransform.Rotation.Rotator();
    Config.SetString("Visual", "MeshRotation", FormatVector(
        FVector3(MeshRotation.Pitch, MeshRotation.Yaw, MeshRotation.Roll)));
    Config.SetString("Visual", "MeshScale", FormatVector(Profile.MeshTransform.Scale));
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
    FVector3 RotationEuler;
    if (!ParseVector(Config.GetString("Visual", "MeshLocation", ""),
            FVector3::ZeroVector, Profile.MeshTransform.Translation)
        || !ParseVector(Config.GetString("Visual", "MeshRotation", ""),
            FVector3::ZeroVector, RotationEuler)
        || !ParseVector(Config.GetString("Visual", "MeshScale", ""),
            FVector3::OneVector, Profile.MeshTransform.Scale))
    {
        Report(OutError, ECharacterProfileError::InvalidData);
        return false;
    }
    Profile.MeshTransform.Rotation = FRotator(
        RotationEuler.X, RotationEuler.Y, RotationEuler.Z).Quaternion();
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
