#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/Types.h"

#include <filesystem>
#include <string_view>

namespace Pico
{
enum class EThirdPersonMovementReference : int32
{
    ControlRotation = 0,
    ActorRotation = 1,
    World = 2
};

enum class EThirdPersonControlProfileError
{
    None,
    InvalidArgument,
    InvalidData,
    FileReadFailed,
    FileWriteFailed
};

struct FThirdPersonControlProfileData
{
    int32 Version = 1;
    EThirdPersonMovementReference MovementReference =
        EThirdPersonMovementReference::ControlRotation;
    float MaxWalkSpeed = 250.0f;
    float RotationRate = 540.0f;
    bool bOrientRotationToMovement = true;
    bool bUseControllerRotationYaw = false;
    bool bUseControllerDesiredRotationWhenAiming = true;
    float DefaultCameraArmLength = 420.0f;
    float AimCameraArmLength = 340.0f;
    FVector3 AimCameraSocketOffset {0.0f, 65.0f, 10.0f};
    float InitialCameraPitch = -15.0f;
    float MinimumCameraPitch = -75.0f;
    float MaximumCameraPitch = 55.0f;
    bool bCameraUsesControlRotation = true;
};

struct FThirdPersonMovementBasis
{
    FVector3 Forward = FVector3::ForwardVector;
    FVector3 ScreenRight {0.0f, -1.0f, 0.0f};
};

bool ValidateThirdPersonControlProfile(
    const FThirdPersonControlProfileData& Profile,
    EThirdPersonControlProfileError* OutError = nullptr);
bool SaveThirdPersonControlProfileToFile(
    const std::filesystem::path& FilePath,
    const FThirdPersonControlProfileData& Profile,
    EThirdPersonControlProfileError* OutError = nullptr);
bool LoadThirdPersonControlProfileFromFile(
    const std::filesystem::path& FilePath,
    FThirdPersonControlProfileData& OutProfile,
    EThirdPersonControlProfileError* OutError = nullptr);

FThirdPersonMovementBasis BuildThirdPersonMovementBasis(
    EThirdPersonMovementReference Reference,
    float ControlYawDegrees,
    float ActorYawDegrees);
uint64 HashThirdPersonControlProfile(const FThirdPersonControlProfileData& Profile);

std::string_view ToString(EThirdPersonMovementReference Reference);
std::string_view ToString(EThirdPersonControlProfileError Error);
}
