#pragma once

#include "Pico/Core/Math/MathUtility.h"

namespace Pico
{
struct FQuat;

struct FRotator
{
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    float Roll = 0.0f;

    constexpr FRotator() = default;
    constexpr FRotator(float InPitch, float InYaw, float InRoll)
        : Pitch(InPitch), Yaw(InYaw), Roll(InRoll)
    {
    }

    FQuat Quaternion() const;
    FRotator GetNormalized() const;
    bool Equals(const FRotator& Other, float Tolerance = KindaSmallNumber) const;

    static float NormalizeAxis(float Angle);
    static const FRotator ZeroRotator;
};
}
