#include "Pico/PhysicsCore/CollisionTypes.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
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
