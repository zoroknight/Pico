#pragma once

#include "Pico/Core/Math/MathUtility.h"

namespace Pico
{
struct FVector2
{
    float X = 0.0f;
    float Y = 0.0f;

    constexpr FVector2() = default;
    constexpr explicit FVector2(float Value)
        : X(Value), Y(Value)
    {
    }
    constexpr FVector2(float InX, float InY)
        : X(InX), Y(InY)
    {
    }

    bool Equals(const FVector2& Other, float Tolerance = KindaSmallNumber) const
    {
        return IsNearlyEqual(X, Other.X, Tolerance)
            && IsNearlyEqual(Y, Other.Y, Tolerance);
    }
};
}
