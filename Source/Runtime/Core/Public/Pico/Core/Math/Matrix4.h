#pragma once

#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Core/Math/Vector3.h"

#include <cstddef>

namespace Pico
{
struct FTransform;

struct FMatrix4
{
    float M[4][4] {};

    constexpr FMatrix4() = default;

    static FMatrix4 MakeIdentity();
    static FMatrix4 FromTransform(const FTransform& Transform);

    FVector3 TransformPosition(const FVector3& Position) const;
    FVector3 TransformVector(const FVector3& Vector) const;
    bool Equals(const FMatrix4& Other, float Tolerance = KindaSmallNumber) const;
    const float* GetData() const;
    float* GetData();

    FMatrix4 operator*(const FMatrix4& Other) const;
    const float* operator[](std::size_t Row) const;
    float* operator[](std::size_t Row);

    static const FMatrix4 Identity;
};
}
