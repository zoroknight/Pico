#pragma once

#include "Pico/Core/Math/Matrix4.h"
#include "Pico/Core/Math/Quat.h"
#include "Pico/Core/Math/Rotator.h"
#include "Pico/Core/Math/Vector3.h"

namespace Pico
{
struct FTransform
{
    FQuat Rotation = FQuat::Identity;
    FVector3 Translation = FVector3::ZeroVector;
    FVector3 Scale = FVector3::OneVector;

    FTransform() = default;
    explicit FTransform(const FVector3& InTranslation);
    FTransform(const FRotator& InRotation, const FVector3& InTranslation, const FVector3& InScale);
    FTransform(const FQuat& InRotation, const FVector3& InTranslation, const FVector3& InScale);

    FVector3 TransformPosition(const FVector3& Position) const;
    FVector3 TransformVector(const FVector3& Vector) const;
    FVector3 InverseTransformPosition(const FVector3& Position) const;
    FVector3 InverseTransformVector(const FVector3& Vector) const;
    FTransform GetRelativeTransform(const FTransform& Parent) const;
    FMatrix4 ToMatrix() const;
    bool Equals(const FTransform& Other, float Tolerance = KindaSmallNumber) const;

    // Matches UE-style composition: Local * Parent produces World.
    FTransform operator*(const FTransform& Parent) const;

    static const FTransform Identity;
};
}
