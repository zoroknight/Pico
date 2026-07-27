#pragma once

#include <cmath>

namespace Pico
{
inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float SmallNumber = 1.0e-8f;
inline constexpr float KindaSmallNumber = 1.0e-4f;

constexpr float DegreesToRadians(float Degrees)
{
    return Degrees * (Pi / 180.0f);
}

constexpr float RadiansToDegrees(float Radians)
{
    return Radians * (180.0f / Pi);
}

inline bool IsNearlyEqual(float Left, float Right, float Tolerance = KindaSmallNumber)
{
    return std::abs(Left - Right) <= Tolerance;
}
}
