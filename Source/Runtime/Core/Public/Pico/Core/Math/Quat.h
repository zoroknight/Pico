#pragma once

#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Core/Math/Vector3.h"

namespace Pico
{
struct FRotator;

struct FQuat
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;

    constexpr FQuat() = default;
    constexpr FQuat(float InX, float InY, float InZ, float InW)
        : X(InX), Y(InY), Z(InZ), W(InW)
    {
    }

    float SizeSquared() const;
    float Size() const;
    bool Normalize(float Tolerance = SmallNumber);
    FQuat GetNormalized(float Tolerance = SmallNumber) const;
    FQuat Inverse() const;
    FVector3 RotateVector(const FVector3& Vector) const;
    FRotator Rotator() const;
    bool Equals(const FQuat& Other, float Tolerance = KindaSmallNumber) const;

    FQuat operator*(const FQuat& Other) const;

    static FQuat FromAxisAngle(const FVector3& Axis, float AngleRadians);
    static FQuat FromRotator(const FRotator& Rotator);
    static const FQuat Identity;
};
}
