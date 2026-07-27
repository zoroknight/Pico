#pragma once

#include "Pico/Core/Math/MathUtility.h"

namespace Pico
{
struct FVector3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    constexpr FVector3() = default;
    constexpr explicit FVector3(float Value)
        : X(Value), Y(Value), Z(Value)
    {
    }
    constexpr FVector3(float InX, float InY, float InZ)
        : X(InX), Y(InY), Z(InZ)
    {
    }

    float SizeSquared() const;
    float Size() const;
    FVector3 GetSafeNormal(float Tolerance = SmallNumber) const;
    bool Normalize(float Tolerance = SmallNumber);
    bool IsNearlyZero(float Tolerance = KindaSmallNumber) const;
    bool Equals(const FVector3& Other, float Tolerance = KindaSmallNumber) const;

    static float Dot(const FVector3& Left, const FVector3& Right);
    static FVector3 Cross(const FVector3& Left, const FVector3& Right);

    constexpr FVector3 operator+() const
    {
        return *this;
    }

    constexpr FVector3 operator-() const
    {
        return FVector3(-X, -Y, -Z);
    }

    constexpr FVector3 operator+(const FVector3& Other) const
    {
        return FVector3(X + Other.X, Y + Other.Y, Z + Other.Z);
    }

    constexpr FVector3 operator-(const FVector3& Other) const
    {
        return FVector3(X - Other.X, Y - Other.Y, Z - Other.Z);
    }

    constexpr FVector3 operator*(const FVector3& Other) const
    {
        return FVector3(X * Other.X, Y * Other.Y, Z * Other.Z);
    }

    constexpr FVector3 operator/(const FVector3& Other) const
    {
        return FVector3(X / Other.X, Y / Other.Y, Z / Other.Z);
    }

    constexpr FVector3 operator*(float Scalar) const
    {
        return FVector3(X * Scalar, Y * Scalar, Z * Scalar);
    }

    constexpr FVector3 operator/(float Scalar) const
    {
        return FVector3(X / Scalar, Y / Scalar, Z / Scalar);
    }

    FVector3& operator+=(const FVector3& Other);
    FVector3& operator-=(const FVector3& Other);
    FVector3& operator*=(float Scalar);
    FVector3& operator/=(float Scalar);

    static const FVector3 ZeroVector;
    static const FVector3 OneVector;
    static const FVector3 ForwardVector;
    static const FVector3 RightVector;
    static const FVector3 UpVector;
};

constexpr FVector3 operator*(float Scalar, const FVector3& Vector)
{
    return Vector * Scalar;
}
}
