#include "Pico/Asset/ThirdPersonControlProfile.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/Math/Quat.h"
#include "Pico/Core/Math/Rotator.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

namespace Pico
{
namespace
{
void Report(
    EThirdPersonControlProfileError* OutError,
    EThirdPersonControlProfileError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

std::string FormatVector(const FVector3& Value)
{
    std::ostringstream Stream;
    Stream << Value.X << ',' << Value.Y << ',' << Value.Z;
    return Stream.str();
}

std::string FormatFloat(float Value)
{
    std::ostringstream Stream;
    Stream.precision(std::numeric_limits<float>::max_digits10);
    Stream << Value;
    return Stream.str();
}

const char* FormatBool(bool bValue)
{
    return bValue ? "true" : "false";
}

bool ParseVector(const std::string& Text, FVector3& OutValue)
{
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
    if (!Stream.eof() || !std::isfinite(Value.X)
        || !std::isfinite(Value.Y) || !std::isfinite(Value.Z))
    {
        return false;
    }
    OutValue = Value;
    return true;
}

bool TryParseMovementReference(
    std::string_view Text,
    EThirdPersonMovementReference& OutReference)
{
    if (Text == "ControlRotation")
        OutReference = EThirdPersonMovementReference::ControlRotation;
    else if (Text == "ActorRotation")
        OutReference = EThirdPersonMovementReference::ActorRotation;
    else if (Text == "World")
        OutReference = EThirdPersonMovementReference::World;
    else return false;
    return true;
}

void HashBytes(uint64& Hash, const void* Data, std::size_t Size)
{
    const auto* Bytes = static_cast<const unsigned char*>(Data);
    for (std::size_t Index = 0; Index < Size; ++Index)
    {
        Hash ^= Bytes[Index];
        Hash *= 1099511628211ull;
    }
}

template<typename T>
void HashValue(uint64& Hash, const T& Value)
{
    HashBytes(Hash, &Value, sizeof(Value));
}
}

bool ValidateThirdPersonControlProfile(
    const FThirdPersonControlProfileData& Profile,
    EThirdPersonControlProfileError* OutError)
{
    Report(OutError, EThirdPersonControlProfileError::None);
    const int32 Reference = static_cast<int32>(Profile.MovementReference);
    const bool bValid = Profile.Version == 1
        && Reference >= static_cast<int32>(EThirdPersonMovementReference::ControlRotation)
        && Reference <= static_cast<int32>(EThirdPersonMovementReference::World)
        && std::isfinite(Profile.MaxWalkSpeed) && Profile.MaxWalkSpeed > 0.0f
        && std::isfinite(Profile.RotationRate) && Profile.RotationRate >= 0.0f
        && std::isfinite(Profile.DefaultCameraArmLength)
        && Profile.DefaultCameraArmLength >= 0.0f
        && std::isfinite(Profile.AimCameraArmLength)
        && Profile.AimCameraArmLength >= 0.0f
        && std::isfinite(Profile.InitialCameraPitch)
        && std::isfinite(Profile.MinimumCameraPitch)
        && std::isfinite(Profile.MaximumCameraPitch)
        && Profile.MinimumCameraPitch < Profile.MaximumCameraPitch
        && Profile.InitialCameraPitch >= Profile.MinimumCameraPitch
        && Profile.InitialCameraPitch <= Profile.MaximumCameraPitch
        && std::isfinite(Profile.AimCameraSocketOffset.X)
        && std::isfinite(Profile.AimCameraSocketOffset.Y)
        && std::isfinite(Profile.AimCameraSocketOffset.Z);
    if (!bValid) Report(OutError, EThirdPersonControlProfileError::InvalidData);
    return bValid;
}

bool SaveThirdPersonControlProfileToFile(
    const std::filesystem::path& FilePath,
    const FThirdPersonControlProfileData& Profile,
    EThirdPersonControlProfileError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, EThirdPersonControlProfileError::InvalidArgument);
        return false;
    }
    if (!ValidateThirdPersonControlProfile(Profile, OutError)) return false;
    FConfigFile Config;
    Config.SetString("Profile", "Version", std::to_string(Profile.Version));
    Config.SetString("Movement", "Reference", std::string(ToString(Profile.MovementReference)));
    Config.SetString("Movement", "MaxWalkSpeed", FormatFloat(Profile.MaxWalkSpeed));
    Config.SetString("Movement", "RotationRate", FormatFloat(Profile.RotationRate));
    Config.SetString("Movement", "OrientRotationToMovement",
        FormatBool(Profile.bOrientRotationToMovement));
    Config.SetString("Movement", "UseControllerRotationYaw",
        FormatBool(Profile.bUseControllerRotationYaw));
    Config.SetString("Movement", "UseControllerDesiredRotationWhenAiming",
        FormatBool(Profile.bUseControllerDesiredRotationWhenAiming));
    Config.SetString("Camera", "DefaultArmLength",
        FormatFloat(Profile.DefaultCameraArmLength));
    Config.SetString("Camera", "AimArmLength", FormatFloat(Profile.AimCameraArmLength));
    Config.SetString("Camera", "AimSocketOffset", FormatVector(Profile.AimCameraSocketOffset));
    Config.SetString("Camera", "InitialPitch", FormatFloat(Profile.InitialCameraPitch));
    Config.SetString("Camera", "MinimumPitch", FormatFloat(Profile.MinimumCameraPitch));
    Config.SetString("Camera", "MaximumPitch", FormatFloat(Profile.MaximumCameraPitch));
    Config.SetString("Camera", "UsePawnControlRotation",
        FormatBool(Profile.bCameraUsesControlRotation));
    if (!Config.Save(FilePath))
    {
        Report(OutError, EThirdPersonControlProfileError::FileWriteFailed);
        return false;
    }
    Report(OutError, EThirdPersonControlProfileError::None);
    return true;
}

bool LoadThirdPersonControlProfileFromFile(
    const std::filesystem::path& FilePath,
    FThirdPersonControlProfileData& OutProfile,
    EThirdPersonControlProfileError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, EThirdPersonControlProfileError::InvalidArgument);
        return false;
    }
    FConfigFile Config;
    if (!Config.Load(FilePath))
    {
        Report(OutError, EThirdPersonControlProfileError::FileReadFailed);
        return false;
    }
    FThirdPersonControlProfileData Profile;
    Profile.Version = Config.GetInt("Profile", "Version", 0);
    if (!TryParseMovementReference(
            Config.GetString("Movement", "Reference", ""), Profile.MovementReference))
    {
        Report(OutError, EThirdPersonControlProfileError::InvalidData);
        return false;
    }
    Profile.MaxWalkSpeed = static_cast<float>(Config.GetDouble(
        "Movement", "MaxWalkSpeed", Profile.MaxWalkSpeed));
    Profile.RotationRate = static_cast<float>(Config.GetDouble(
        "Movement", "RotationRate", Profile.RotationRate));
    Profile.bOrientRotationToMovement = Config.GetBool(
        "Movement", "OrientRotationToMovement", Profile.bOrientRotationToMovement);
    Profile.bUseControllerRotationYaw = Config.GetBool(
        "Movement", "UseControllerRotationYaw", Profile.bUseControllerRotationYaw);
    Profile.bUseControllerDesiredRotationWhenAiming = Config.GetBool(
        "Movement", "UseControllerDesiredRotationWhenAiming",
        Profile.bUseControllerDesiredRotationWhenAiming);
    Profile.DefaultCameraArmLength = static_cast<float>(Config.GetDouble(
        "Camera", "DefaultArmLength", Profile.DefaultCameraArmLength));
    Profile.AimCameraArmLength = static_cast<float>(Config.GetDouble(
        "Camera", "AimArmLength", Profile.AimCameraArmLength));
    if (!ParseVector(Config.GetString("Camera", "AimSocketOffset", ""),
            Profile.AimCameraSocketOffset))
    {
        Report(OutError, EThirdPersonControlProfileError::InvalidData);
        return false;
    }
    Profile.InitialCameraPitch = static_cast<float>(Config.GetDouble(
        "Camera", "InitialPitch", Profile.InitialCameraPitch));
    Profile.MinimumCameraPitch = static_cast<float>(Config.GetDouble(
        "Camera", "MinimumPitch", Profile.MinimumCameraPitch));
    Profile.MaximumCameraPitch = static_cast<float>(Config.GetDouble(
        "Camera", "MaximumPitch", Profile.MaximumCameraPitch));
    Profile.bCameraUsesControlRotation = Config.GetBool(
        "Camera", "UsePawnControlRotation", Profile.bCameraUsesControlRotation);
    if (!ValidateThirdPersonControlProfile(Profile, OutError)) return false;
    OutProfile = Profile;
    return true;
}

FThirdPersonMovementBasis BuildThirdPersonMovementBasis(
    EThirdPersonMovementReference Reference,
    float ControlYawDegrees,
    float ActorYawDegrees)
{
    float BasisYaw = 0.0f;
    if (Reference == EThirdPersonMovementReference::ControlRotation)
        BasisYaw = ControlYawDegrees;
    else if (Reference == EThirdPersonMovementReference::ActorRotation)
        BasisYaw = ActorYawDegrees;
    const FQuat YawRotation = FQuat::FromRotator(
        FRotator(0.0f, BasisYaw, 0.0f));
    FThirdPersonMovementBasis Result;
    Result.Forward = YawRotation.RotateVector(FVector3::ForwardVector).GetSafeNormal();
    // Pico's camera convention treats Forward x Up as screen-right.
    Result.ScreenRight = FVector3::Cross(Result.Forward, FVector3::UpVector).GetSafeNormal();
    return Result;
}

uint64 HashThirdPersonControlProfile(const FThirdPersonControlProfileData& Profile)
{
    uint64 Hash = 14695981039346656037ull;
    HashValue(Hash, Profile.Version);
    HashValue(Hash, Profile.MovementReference);
    HashValue(Hash, Profile.MaxWalkSpeed);
    HashValue(Hash, Profile.RotationRate);
    HashValue(Hash, Profile.bOrientRotationToMovement);
    HashValue(Hash, Profile.bUseControllerRotationYaw);
    HashValue(Hash, Profile.bUseControllerDesiredRotationWhenAiming);
    HashValue(Hash, Profile.DefaultCameraArmLength);
    HashValue(Hash, Profile.AimCameraArmLength);
    HashValue(Hash, Profile.AimCameraSocketOffset);
    HashValue(Hash, Profile.InitialCameraPitch);
    HashValue(Hash, Profile.MinimumCameraPitch);
    HashValue(Hash, Profile.MaximumCameraPitch);
    HashValue(Hash, Profile.bCameraUsesControlRotation);
    return Hash;
}

std::string_view ToString(EThirdPersonMovementReference Reference)
{
    switch (Reference)
    {
    case EThirdPersonMovementReference::ControlRotation: return "ControlRotation";
    case EThirdPersonMovementReference::ActorRotation: return "ActorRotation";
    case EThirdPersonMovementReference::World: return "World";
    }
    return "Unknown";
}

std::string_view ToString(EThirdPersonControlProfileError Error)
{
    switch (Error)
    {
    case EThirdPersonControlProfileError::None: return "None";
    case EThirdPersonControlProfileError::InvalidArgument: return "InvalidArgument";
    case EThirdPersonControlProfileError::InvalidData: return "InvalidData";
    case EThirdPersonControlProfileError::FileReadFailed: return "FileReadFailed";
    case EThirdPersonControlProfileError::FileWriteFailed: return "FileWriteFailed";
    }
    return "Unknown";
}
}
