#include "Pico/Core/Math/Math.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
const FVector3 FVector3::ZeroVector(0.0f, 0.0f, 0.0f);
const FVector3 FVector3::OneVector(1.0f, 1.0f, 1.0f);
const FVector3 FVector3::ForwardVector(1.0f, 0.0f, 0.0f);
const FVector3 FVector3::RightVector(0.0f, 1.0f, 0.0f);
const FVector3 FVector3::UpVector(0.0f, 0.0f, 1.0f);
const FRotator FRotator::ZeroRotator(0.0f, 0.0f, 0.0f);
const FQuat FQuat::Identity(0.0f, 0.0f, 0.0f, 1.0f);
const FMatrix4 FMatrix4::Identity = FMatrix4::MakeIdentity();
const FTransform FTransform::Identity;

float FVector3::SizeSquared() const
{
    return X * X + Y * Y + Z * Z;
}

float FVector3::Size() const
{
    return std::sqrt(SizeSquared());
}

FVector3 FVector3::GetSafeNormal(float Tolerance) const
{
    const float SquaredLength = SizeSquared();
    if (SquaredLength <= Tolerance)
    {
        return ZeroVector;
    }
    return *this / std::sqrt(SquaredLength);
}

bool FVector3::Normalize(float Tolerance)
{
    const float SquaredLength = SizeSquared();
    if (SquaredLength <= Tolerance)
    {
        *this = ZeroVector;
        return false;
    }

    *this /= std::sqrt(SquaredLength);
    return true;
}

bool FVector3::IsNearlyZero(float Tolerance) const
{
    return std::abs(X) <= Tolerance
        && std::abs(Y) <= Tolerance
        && std::abs(Z) <= Tolerance;
}

bool FVector3::Equals(const FVector3& Other, float Tolerance) const
{
    return IsNearlyEqual(X, Other.X, Tolerance)
        && IsNearlyEqual(Y, Other.Y, Tolerance)
        && IsNearlyEqual(Z, Other.Z, Tolerance);
}

float FVector3::Dot(const FVector3& Left, const FVector3& Right)
{
    return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}

FVector3 FVector3::Cross(const FVector3& Left, const FVector3& Right)
{
    return FVector3(
        Left.Y * Right.Z - Left.Z * Right.Y,
        Left.Z * Right.X - Left.X * Right.Z,
        Left.X * Right.Y - Left.Y * Right.X);
}

FVector3& FVector3::operator+=(const FVector3& Other)
{
    X += Other.X;
    Y += Other.Y;
    Z += Other.Z;
    return *this;
}

FVector3& FVector3::operator-=(const FVector3& Other)
{
    X -= Other.X;
    Y -= Other.Y;
    Z -= Other.Z;
    return *this;
}

FVector3& FVector3::operator*=(float Scalar)
{
    X *= Scalar;
    Y *= Scalar;
    Z *= Scalar;
    return *this;
}

FVector3& FVector3::operator/=(float Scalar)
{
    X /= Scalar;
    Y /= Scalar;
    Z /= Scalar;
    return *this;
}

float FRotator::NormalizeAxis(float Angle)
{
    Angle = std::fmod(Angle, 360.0f);
    if (Angle > 180.0f)
    {
        Angle -= 360.0f;
    }
    else if (Angle < -180.0f)
    {
        Angle += 360.0f;
    }
    return Angle;
}

FQuat FRotator::Quaternion() const
{
    return FQuat::FromRotator(*this);
}

FRotator FRotator::GetNormalized() const
{
    return FRotator(NormalizeAxis(Pitch), NormalizeAxis(Yaw), NormalizeAxis(Roll));
}

bool FRotator::Equals(const FRotator& Other, float Tolerance) const
{
    const FRotator Delta(
        NormalizeAxis(Pitch - Other.Pitch),
        NormalizeAxis(Yaw - Other.Yaw),
        NormalizeAxis(Roll - Other.Roll));
    return std::abs(Delta.Pitch) <= Tolerance
        && std::abs(Delta.Yaw) <= Tolerance
        && std::abs(Delta.Roll) <= Tolerance;
}

float FQuat::SizeSquared() const
{
    return X * X + Y * Y + Z * Z + W * W;
}

float FQuat::Size() const
{
    return std::sqrt(SizeSquared());
}

bool FQuat::Normalize(float Tolerance)
{
    const float SquaredLength = SizeSquared();
    if (SquaredLength <= Tolerance)
    {
        *this = Identity;
        return false;
    }

    const float InverseLength = 1.0f / std::sqrt(SquaredLength);
    X *= InverseLength;
    Y *= InverseLength;
    Z *= InverseLength;
    W *= InverseLength;
    return true;
}

FQuat FQuat::GetNormalized(float Tolerance) const
{
    FQuat Result = *this;
    Result.Normalize(Tolerance);
    return Result;
}

FQuat FQuat::Inverse() const
{
    const float SquaredLength = SizeSquared();
    if (SquaredLength <= SmallNumber)
    {
        return Identity;
    }

    const float InverseSquaredLength = 1.0f / SquaredLength;
    return FQuat(
        -X * InverseSquaredLength,
        -Y * InverseSquaredLength,
        -Z * InverseSquaredLength,
        W * InverseSquaredLength);
}

FVector3 FQuat::RotateVector(const FVector3& Vector) const
{
    const FQuat Normalized = GetNormalized();
    const FVector3 QuaternionVector(Normalized.X, Normalized.Y, Normalized.Z);
    const FVector3 TwiceCross = 2.0f * FVector3::Cross(QuaternionVector, Vector);
    return Vector
        + Normalized.W * TwiceCross
        + FVector3::Cross(QuaternionVector, TwiceCross);
}

FRotator FQuat::Rotator() const
{
    const FQuat Q = GetNormalized();
    const float SinPitch = std::clamp(2.0f * (Q.W * Q.Y - Q.Z * Q.X), -1.0f, 1.0f);
    const float Pitch = std::asin(SinPitch);
    const float Yaw = std::atan2(
        2.0f * (Q.X * Q.Y + Q.W * Q.Z),
        1.0f - 2.0f * (Q.Y * Q.Y + Q.Z * Q.Z));
    const float Roll = std::atan2(
        2.0f * (Q.Y * Q.Z + Q.W * Q.X),
        1.0f - 2.0f * (Q.X * Q.X + Q.Y * Q.Y));
    return FRotator(
        RadiansToDegrees(Pitch),
        RadiansToDegrees(Yaw),
        RadiansToDegrees(Roll)).GetNormalized();
}

bool FQuat::Equals(const FQuat& Other, float Tolerance) const
{
    const bool bDirect =
        IsNearlyEqual(X, Other.X, Tolerance)
        && IsNearlyEqual(Y, Other.Y, Tolerance)
        && IsNearlyEqual(Z, Other.Z, Tolerance)
        && IsNearlyEqual(W, Other.W, Tolerance);
    const bool bNegated =
        IsNearlyEqual(X, -Other.X, Tolerance)
        && IsNearlyEqual(Y, -Other.Y, Tolerance)
        && IsNearlyEqual(Z, -Other.Z, Tolerance)
        && IsNearlyEqual(W, -Other.W, Tolerance);
    return bDirect || bNegated;
}

FQuat FQuat::operator*(const FQuat& Other) const
{
    return FQuat(
        W * Other.X + X * Other.W + Y * Other.Z - Z * Other.Y,
        W * Other.Y - X * Other.Z + Y * Other.W + Z * Other.X,
        W * Other.Z + X * Other.Y - Y * Other.X + Z * Other.W,
        W * Other.W - X * Other.X - Y * Other.Y - Z * Other.Z);
}

FQuat FQuat::FromAxisAngle(const FVector3& Axis, float AngleRadians)
{
    const FVector3 NormalizedAxis = Axis.GetSafeNormal();
    if (NormalizedAxis.IsNearlyZero())
    {
        return Identity;
    }

    const float HalfAngle = AngleRadians * 0.5f;
    const float SinHalfAngle = std::sin(HalfAngle);
    return FQuat(
        NormalizedAxis.X * SinHalfAngle,
        NormalizedAxis.Y * SinHalfAngle,
        NormalizedAxis.Z * SinHalfAngle,
        std::cos(HalfAngle)).GetNormalized();
}

FQuat FQuat::FromRotator(const FRotator& Rotator)
{
    const float HalfPitch = DegreesToRadians(Rotator.Pitch) * 0.5f;
    const float HalfYaw = DegreesToRadians(Rotator.Yaw) * 0.5f;
    const float HalfRoll = DegreesToRadians(Rotator.Roll) * 0.5f;

    const FQuat PitchQuat(0.0f, std::sin(HalfPitch), 0.0f, std::cos(HalfPitch));
    const FQuat YawQuat(0.0f, 0.0f, std::sin(HalfYaw), std::cos(HalfYaw));
    const FQuat RollQuat(std::sin(HalfRoll), 0.0f, 0.0f, std::cos(HalfRoll));
    return (YawQuat * PitchQuat * RollQuat).GetNormalized();
}

FMatrix4 FMatrix4::MakeIdentity()
{
    FMatrix4 Result;
    for (std::size_t Index = 0; Index < 4; ++Index)
    {
        Result.M[Index][Index] = 1.0f;
    }
    return Result;
}

FMatrix4 FMatrix4::FromTransform(const FTransform& Transform)
{
    const FQuat Q = Transform.Rotation.GetNormalized();
    const float XX = Q.X * Q.X;
    const float YY = Q.Y * Q.Y;
    const float ZZ = Q.Z * Q.Z;
    const float XY = Q.X * Q.Y;
    const float XZ = Q.X * Q.Z;
    const float YZ = Q.Y * Q.Z;
    const float WX = Q.W * Q.X;
    const float WY = Q.W * Q.Y;
    const float WZ = Q.W * Q.Z;

    FMatrix4 Result = Identity;
    Result.M[0][0] = (1.0f - 2.0f * (YY + ZZ)) * Transform.Scale.X;
    Result.M[0][1] = (2.0f * (XY - WZ)) * Transform.Scale.Y;
    Result.M[0][2] = (2.0f * (XZ + WY)) * Transform.Scale.Z;
    Result.M[1][0] = (2.0f * (XY + WZ)) * Transform.Scale.X;
    Result.M[1][1] = (1.0f - 2.0f * (XX + ZZ)) * Transform.Scale.Y;
    Result.M[1][2] = (2.0f * (YZ - WX)) * Transform.Scale.Z;
    Result.M[2][0] = (2.0f * (XZ - WY)) * Transform.Scale.X;
    Result.M[2][1] = (2.0f * (YZ + WX)) * Transform.Scale.Y;
    Result.M[2][2] = (1.0f - 2.0f * (XX + YY)) * Transform.Scale.Z;
    Result.M[0][3] = Transform.Translation.X;
    Result.M[1][3] = Transform.Translation.Y;
    Result.M[2][3] = Transform.Translation.Z;
    return Result;
}

FVector3 FMatrix4::TransformPosition(const FVector3& Position) const
{
    const float X = M[0][0] * Position.X + M[0][1] * Position.Y + M[0][2] * Position.Z + M[0][3];
    const float Y = M[1][0] * Position.X + M[1][1] * Position.Y + M[1][2] * Position.Z + M[1][3];
    const float Z = M[2][0] * Position.X + M[2][1] * Position.Y + M[2][2] * Position.Z + M[2][3];
    const float W = M[3][0] * Position.X + M[3][1] * Position.Y + M[3][2] * Position.Z + M[3][3];
    return std::abs(W) > SmallNumber ? FVector3(X, Y, Z) / W : FVector3(X, Y, Z);
}

FVector3 FMatrix4::TransformVector(const FVector3& Vector) const
{
    return FVector3(
        M[0][0] * Vector.X + M[0][1] * Vector.Y + M[0][2] * Vector.Z,
        M[1][0] * Vector.X + M[1][1] * Vector.Y + M[1][2] * Vector.Z,
        M[2][0] * Vector.X + M[2][1] * Vector.Y + M[2][2] * Vector.Z);
}

bool FMatrix4::Equals(const FMatrix4& Other, float Tolerance) const
{
    for (std::size_t Row = 0; Row < 4; ++Row)
    {
        for (std::size_t Column = 0; Column < 4; ++Column)
        {
            if (!IsNearlyEqual(M[Row][Column], Other.M[Row][Column], Tolerance))
            {
                return false;
            }
        }
    }
    return true;
}

const float* FMatrix4::GetData() const
{
    return &M[0][0];
}

float* FMatrix4::GetData()
{
    return &M[0][0];
}

FMatrix4 FMatrix4::operator*(const FMatrix4& Other) const
{
    FMatrix4 Result;
    for (std::size_t Row = 0; Row < 4; ++Row)
    {
        for (std::size_t Column = 0; Column < 4; ++Column)
        {
            for (std::size_t Inner = 0; Inner < 4; ++Inner)
            {
                Result.M[Row][Column] += M[Row][Inner] * Other.M[Inner][Column];
            }
        }
    }
    return Result;
}

const float* FMatrix4::operator[](std::size_t Row) const
{
    return M[Row];
}

float* FMatrix4::operator[](std::size_t Row)
{
    return M[Row];
}

FTransform::FTransform(const FVector3& InTranslation)
    : Translation(InTranslation)
{
}

FTransform::FTransform(const FRotator& InRotation, const FVector3& InTranslation, const FVector3& InScale)
    : Rotation(InRotation.Quaternion())
    , Translation(InTranslation)
    , Scale(InScale)
{
}

FTransform::FTransform(const FQuat& InRotation, const FVector3& InTranslation, const FVector3& InScale)
    : Rotation(InRotation.GetNormalized())
    , Translation(InTranslation)
    , Scale(InScale)
{
}

FVector3 FTransform::TransformPosition(const FVector3& Position) const
{
    return Rotation.RotateVector(Scale * Position) + Translation;
}

FVector3 FTransform::TransformVector(const FVector3& Vector) const
{
    return Rotation.RotateVector(Scale * Vector);
}

FVector3 FTransform::InverseTransformPosition(const FVector3& Position) const
{
    return InverseTransformVector(Position - Translation);
}

FVector3 FTransform::InverseTransformVector(const FVector3& Vector) const
{
    const FVector3 Unrotated = Rotation.Inverse().RotateVector(Vector);
    return FVector3(
        std::abs(Scale.X) > SmallNumber ? Unrotated.X / Scale.X : 0.0f,
        std::abs(Scale.Y) > SmallNumber ? Unrotated.Y / Scale.Y : 0.0f,
        std::abs(Scale.Z) > SmallNumber ? Unrotated.Z / Scale.Z : 0.0f);
}

FTransform FTransform::GetRelativeTransform(const FTransform& Parent) const
{
    const FVector3 RelativeScale(
        std::abs(Parent.Scale.X) > SmallNumber ? Scale.X / Parent.Scale.X : 0.0f,
        std::abs(Parent.Scale.Y) > SmallNumber ? Scale.Y / Parent.Scale.Y : 0.0f,
        std::abs(Parent.Scale.Z) > SmallNumber ? Scale.Z / Parent.Scale.Z : 0.0f);
    return FTransform(
        Parent.Rotation.Inverse() * Rotation,
        Parent.InverseTransformPosition(Translation),
        RelativeScale);
}

FMatrix4 FTransform::ToMatrix() const
{
    return FMatrix4::FromTransform(*this);
}

bool FTransform::Equals(const FTransform& Other, float Tolerance) const
{
    return Rotation.Equals(Other.Rotation, Tolerance)
        && Translation.Equals(Other.Translation, Tolerance)
        && Scale.Equals(Other.Scale, Tolerance);
}

FTransform FTransform::operator*(const FTransform& Parent) const
{
    return FTransform(
        Parent.Rotation * Rotation,
        Parent.TransformPosition(Translation),
        Parent.Scale * Scale);
}
}
