#pragma once

#include "Pico/Core/Math/Quat.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/Types.h"
#include "Pico/Object/ObjectTypes.h"

#include <vector>

namespace Pico
{
enum class ECollisionShapeType : uint8
{
    Point,
    Box,
    Sphere,
    Capsule
};

enum class ECollisionEnabled : uint8
{
    NoCollision,
    QueryOnly,
    PhysicsOnly,
    QueryAndPhysics
};

constexpr bool HasQueryCollision(ECollisionEnabled CollisionEnabled)
{
    return CollisionEnabled == ECollisionEnabled::QueryOnly
        || CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
}

constexpr bool HasPhysicsCollision(ECollisionEnabled CollisionEnabled)
{
    return CollisionEnabled == ECollisionEnabled::PhysicsOnly
        || CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
}

enum class EPhysicsBodyType : uint8
{
    Static,
    Kinematic,
    Dynamic
};

struct FPhysicsBodyHandle
{
    bool IsValid() const { return Id != 0 && Serial != 0; }
    friend bool operator==(const FPhysicsBodyHandle&, const FPhysicsBodyHandle&) = default;

    uint32 Id = 0;
    uint32 Serial = 0;
};

enum class EMoveComponentFlags : uint8
{
    None = 0,
    IgnoreBases = 1 << 0
};

constexpr EMoveComponentFlags operator|(
    EMoveComponentFlags Left,
    EMoveComponentFlags Right)
{
    return static_cast<EMoveComponentFlags>(
        static_cast<uint8>(Left) | static_cast<uint8>(Right));
}

enum class ETeleportType : uint8
{
    None,
    TeleportPhysics,
    ResetPhysics
};

struct FCollisionShape
{
    ECollisionShapeType Type = ECollisionShapeType::Point;
    FVector3 Extent = FVector3::ZeroVector;
    float Radius = 0.0f;
    float HalfHeight = 0.0f;

    static FCollisionShape MakePoint();
    static FCollisionShape MakeBox(const FVector3& HalfExtent);
    static FCollisionShape MakeSphere(float InRadius);
    static FCollisionShape MakeCapsule(float InRadius, float InHalfHeight);
    bool IsValid() const;
};

struct FCollisionQueryParams
{
    FObjectHandle MovingObject;
    std::vector<FObjectHandle> IgnoredObjects;
    bool bTraceComplex = false;
    bool bIgnoreSensors = true;

    bool IsIgnored(FObjectHandle Handle) const;
};

struct FPhysicsBodyDesc
{
    FObjectHandle OwnerObject;
    FCollisionShape Shape;
    FTransform Transform;
    ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
    EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
    float Mass = 1.0f;
    bool bUseGravity = true;
    bool bSensor = false;
};

struct FPhysicsBodyState
{
    FTransform Transform;
    FVector3 LinearVelocity = FVector3::ZeroVector;
    bool bActive = false;
};

struct FHitResult
{
    bool bBlockingHit = false;
    bool bStartPenetrating = false;
    float Time = 1.0f;
    float Distance = 0.0f;
    float PenetrationDepth = 0.0f;
    FVector3 Location = FVector3::ZeroVector;
    FVector3 ImpactPoint = FVector3::ZeroVector;
    FVector3 Normal = FVector3::ZeroVector;
    FVector3 ImpactNormal = FVector3::ZeroVector;
    FVector3 TraceStart = FVector3::ZeroVector;
    FVector3 TraceEnd = FVector3::ZeroVector;
    FObjectHandle HitObject;

    void Reset(const FVector3& Start, const FVector3& End);
};

struct FOverlapResult
{
    FObjectHandle OverlapObject;
};

enum class EPhysicsContactEventType : uint8
{
    Begin,
    Persist,
    End
};

struct FPhysicsContactEvent
{
    EPhysicsContactEventType Type = EPhysicsContactEventType::Begin;
    FObjectHandle ObjectA;
    FObjectHandle ObjectB;
    FVector3 Location = FVector3::ZeroVector;
    FVector3 Normal = FVector3::ZeroVector;
    bool bSensor = false;
};
}
