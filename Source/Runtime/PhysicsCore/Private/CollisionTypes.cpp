#include "Pico/PhysicsCore/CollisionTypes.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
FCollisionFilterData::FCollisionFilterData()
{
    SetAllResponses(ECollisionResponse::Block);
}

ECollisionResponse FCollisionFilterData::GetResponse(
    ECollisionChannel Channel) const
{
    const std::size_t Index = static_cast<std::size_t>(Channel);
    return Index < Responses.size()
        ? Responses[Index]
        : ECollisionResponse::Ignore;
}

void FCollisionFilterData::SetResponse(
    ECollisionChannel Channel,
    ECollisionResponse Response)
{
    const std::size_t Index = static_cast<std::size_t>(Channel);
    if (Index < Responses.size()) Responses[Index] = Response;
}

void FCollisionFilterData::SetAllResponses(ECollisionResponse Response)
{
    Responses.fill(Response);
}

ECollisionResponse ResolveCollisionResponse(
    const FCollisionFilterData& Left,
    const FCollisionFilterData& Right)
{
    const ECollisionResponse LeftResponse =
        Left.GetResponse(Right.ObjectType);
    const ECollisionResponse RightResponse =
        Right.GetResponse(Left.ObjectType);
    if (LeftResponse == ECollisionResponse::Ignore
        || RightResponse == ECollisionResponse::Ignore)
    {
        return ECollisionResponse::Ignore;
    }
    return LeftResponse == ECollisionResponse::Overlap
            || RightResponse == ECollisionResponse::Overlap
        ? ECollisionResponse::Overlap
        : ECollisionResponse::Block;
}

FCollisionFilterData MakeCollisionFilter(
    ECollisionProfile Profile,
    EPhysicsBodyType BodyType)
{
    FCollisionFilterData Filter;
    if (Profile == ECollisionProfile::Custom)
    {
        Filter.ObjectType = BodyType == EPhysicsBodyType::Static
            ? ECollisionChannel::WorldStatic
            : BodyType == EPhysicsBodyType::Dynamic
                ? ECollisionChannel::PhysicsBody
                : ECollisionChannel::WorldDynamic;
        return Filter;
    }

    switch (Profile)
    {
    case ECollisionProfile::NoCollision:
        Filter.SetAllResponses(ECollisionResponse::Ignore);
        break;
    case ECollisionProfile::BlockAll:
        Filter.ObjectType = ECollisionChannel::WorldStatic;
        break;
    case ECollisionProfile::Pawn:
        Filter.ObjectType = ECollisionChannel::Pawn;
        break;
    case ECollisionProfile::PawnNoPawnCollision:
        Filter.ObjectType = ECollisionChannel::Pawn;
        Filter.SetResponse(
            ECollisionChannel::Pawn, ECollisionResponse::Ignore);
        break;
    case ECollisionProfile::CharacterMesh:
        Filter.ObjectType = ECollisionChannel::Pawn;
        Filter.SetAllResponses(ECollisionResponse::Ignore);
        break;
    case ECollisionProfile::PhysicsActor:
        Filter.ObjectType = ECollisionChannel::PhysicsBody;
        break;
    case ECollisionProfile::Trigger:
        Filter.ObjectType = ECollisionChannel::Trigger;
        Filter.SetAllResponses(ECollisionResponse::Overlap);
        break;
    case ECollisionProfile::Projectile:
        Filter.ObjectType = ECollisionChannel::Projectile;
        Filter.SetAllResponses(ECollisionResponse::Ignore);
        Filter.SetResponse(
            ECollisionChannel::WorldStatic, ECollisionResponse::Block);
        Filter.SetResponse(
            ECollisionChannel::WorldDynamic, ECollisionResponse::Block);
        Filter.SetResponse(
            ECollisionChannel::Pawn, ECollisionResponse::Block);
        Filter.SetResponse(
            ECollisionChannel::PhysicsBody, ECollisionResponse::Block);
        break;
    case ECollisionProfile::Custom:
        break;
    }
    return Filter;
}

ECollisionEnabled GetCollisionProfileEnabled(ECollisionProfile Profile)
{
    switch (Profile)
    {
    case ECollisionProfile::NoCollision:
    case ECollisionProfile::CharacterMesh:
        return ECollisionEnabled::NoCollision;
    case ECollisionProfile::Trigger:
    case ECollisionProfile::Projectile:
        return ECollisionEnabled::QueryOnly;
    case ECollisionProfile::Custom:
        return ECollisionEnabled::NoCollision;
    default:
        return ECollisionEnabled::QueryAndPhysics;
    }
}

bool IsCollisionProfileSensor(ECollisionProfile Profile)
{
    return Profile == ECollisionProfile::Trigger;
}

FCollisionShape FCollisionShape::MakePoint()
{
    return {};
}

FCollisionShape FCollisionShape::MakeBox(const FVector3& HalfExtent)
{
    FCollisionShape Shape;
    Shape.Type = ECollisionShapeType::Box;
    Shape.Extent = HalfExtent;
    return Shape;
}

FCollisionShape FCollisionShape::MakeSphere(float InRadius)
{
    FCollisionShape Shape;
    Shape.Type = ECollisionShapeType::Sphere;
    Shape.Radius = InRadius;
    return Shape;
}

FCollisionShape FCollisionShape::MakeCapsule(float InRadius, float InHalfHeight)
{
    FCollisionShape Shape;
    Shape.Type = ECollisionShapeType::Capsule;
    Shape.Radius = InRadius;
    Shape.HalfHeight = InHalfHeight;
    return Shape;
}

bool FCollisionShape::IsValid() const
{
    switch (Type)
    {
    case ECollisionShapeType::Point:
        return true;
    case ECollisionShapeType::Box:
        return std::isfinite(Extent.X) && std::isfinite(Extent.Y)
            && std::isfinite(Extent.Z) && Extent.X >= 0.0f
            && Extent.Y >= 0.0f && Extent.Z >= 0.0f;
    case ECollisionShapeType::Sphere:
        return std::isfinite(Radius) && Radius >= 0.0f;
    case ECollisionShapeType::Capsule:
        return std::isfinite(Radius) && std::isfinite(HalfHeight)
            && Radius >= 0.0f && HalfHeight >= Radius;
    }
    return false;
}

bool FCollisionQueryParams::IsIgnored(FObjectHandle Handle) const
{
    return std::find(IgnoredObjects.begin(), IgnoredObjects.end(), Handle)
        != IgnoredObjects.end();
}

void FHitResult::Reset(const FVector3& Start, const FVector3& End)
{
    *this = {};
    Time = 1.0f;
    TraceStart = Start;
    TraceEnd = End;
    Location = End;
    ImpactPoint = End;
}
}
