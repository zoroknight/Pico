#include "Pico/PhysicsJolt/JoltPhysicsScene.h"

#include "Pico/PhysicsCore/PhysicsScene.h"
#include "Pico/Core/Log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Pico
{
namespace
{
constexpr float PicoToJoltScale = 0.01f;
constexpr float JoltToPicoScale = 100.0f;
constexpr float FixedPhysicsDeltaSeconds = 1.0f / 60.0f;
constexpr uint32 MaxPhysicsSubsteps = 4;
constexpr float MaxAccumulatedPhysicsTime =
    FixedPhysicsDeltaSeconds * static_cast<float>(MaxPhysicsSubsteps);

namespace Layers
{
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
}

namespace BroadPhaseLayers
{
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint Count = 2;
}

JPH::Vec3 ToJoltVector(const FVector3& Value, float Scale = 1.0f)
{
    return JPH::Vec3(Value.X * Scale, Value.Y * Scale, Value.Z * Scale);
}

JPH::RVec3 ToJoltPosition(const FVector3& Value)
{
    return JPH::RVec3(
        Value.X * PicoToJoltScale,
        Value.Y * PicoToJoltScale,
        Value.Z * PicoToJoltScale);
}

FVector3 FromJoltVector(JPH::Vec3Arg Value, float Scale = 1.0f)
{
    return FVector3(Value.GetX() * Scale, Value.GetY() * Scale, Value.GetZ() * Scale);
}

FVector3 FromJoltPosition(JPH::RVec3Arg Value)
{
    return FVector3(
        static_cast<float>(Value.GetX()) * JoltToPicoScale,
        static_cast<float>(Value.GetY()) * JoltToPicoScale,
        static_cast<float>(Value.GetZ()) * JoltToPicoScale);
}

JPH::Quat ToJoltQuat(const FQuat& Value)
{
    return JPH::Quat(Value.X, Value.Y, Value.Z, Value.W).Normalized();
}

FQuat FromJoltQuat(JPH::QuatArg Value)
{
    return FQuat(Value.GetX(), Value.GetY(), Value.GetZ(), Value.GetW()).GetNormalized();
}

JPH::uint64 EncodeObjectHandle(FObjectHandle Handle)
{
    if (!Handle.IsValid()) return 0;
    return (static_cast<JPH::uint64>(Handle.Serial) << 32)
        | static_cast<JPH::uint64>(Handle.Index);
}

FObjectHandle DecodeObjectHandle(JPH::uint64 Value)
{
    if (Value == 0) return {};
    FObjectHandle Handle;
    Handle.Index = static_cast<uint32>(Value & 0xffffffffu);
    Handle.Serial = static_cast<uint32>(Value >> 32);
    return Handle;
}

class FObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer Left, JPH::ObjectLayer Right) const override
    {
        return Left == Layers::Moving || Right == Layers::Moving;
    }
};

class FBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override
    {
        return Layer == Layers::NonMoving
            ? BroadPhaseLayers::NonMoving
            : BroadPhaseLayers::Moving;
    }
};

class FObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer Layer, JPH::BroadPhaseLayer BroadPhase) const override
    {
        return Layer == Layers::Moving || BroadPhase == BroadPhaseLayers::Moving;
    }
};

struct FBodyCollisionData
{
    FCollisionFilterData Filter;
    ECollisionEnabled CollisionEnabled = ECollisionEnabled::NoCollision;
    bool bExplicitSensor = false;
};

class FQueryBodyFilter final : public JPH::BodyFilter
{
public:
    using FFilterMap = std::unordered_map<uint64, FBodyCollisionData>;

    FQueryBodyFilter(
        const FCollisionQueryParams& InParams,
        const FFilterMap& InFilters,
        bool bInBlockingOnly)
        : Params(InParams)
        , Filters(InFilters)
        , bBlockingOnly(bInBlockingOnly)
    {
    }

    bool ShouldCollideLocked(const JPH::Body& Body) const override
    {
        const FObjectHandle Handle = DecodeObjectHandle(Body.GetUserData());
        if (Handle == Params.MovingObject || Params.IsIgnored(Handle)) return false;
        const auto Moving = Filters.find(EncodeObjectHandle(Params.MovingObject));
        const auto Target = Filters.find(Body.GetUserData());
        if (Target != Filters.end())
        {
            if (!HasQueryCollision(Target->second.CollisionEnabled)
                || (Params.bIgnoreSensors && Target->second.bExplicitSensor))
            {
                return false;
            }
        }
        else if (Params.bIgnoreSensors && Body.IsSensor())
        {
            return false;
        }
        if (Moving == Filters.end() || Target == Filters.end()) return true;
        const ECollisionResponse Response = ResolveCollisionResponse(
            Moving->second.Filter, Target->second.Filter);
        return bBlockingOnly
            ? Response == ECollisionResponse::Block
            : Response != ECollisionResponse::Ignore;
    }

private:
    const FCollisionQueryParams& Params;
    const FFilterMap& Filters;
    bool bBlockingOnly = true;
};

std::mutex RuntimeMutex;
uint32 RuntimeReferences = 0;

void JoltTrace(const char* Format, ...)
{
    char Buffer[1024] {};
    std::va_list Arguments;
    va_start(Arguments, Format);
    std::vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);
    PICO_LOG(LogEngine, Trace, "Jolt: {}", Buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(
    const char* Expression,
    const char* Message,
    const char* File,
    JPH::uint Line)
{
    PICO_LOG(
        LogEngine,
        Error,
        "Jolt assertion at {}:{}: {} ({})",
        File,
        Line,
        Expression,
        Message != nullptr ? Message : "no message");
    return true;
}
#endif

void AcquireJoltRuntime()
{
    const std::scoped_lock Lock(RuntimeMutex);
    if (RuntimeReferences++ == 0)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTrace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

void ReleaseJoltRuntime()
{
    const std::scoped_lock Lock(RuntimeMutex);
    if (RuntimeReferences > 0 && --RuntimeReferences == 0)
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

class FJoltRuntimeGuard
{
public:
    FJoltRuntimeGuard() { AcquireJoltRuntime(); }
    ~FJoltRuntimeGuard() { ReleaseJoltRuntime(); }
};

JPH::RefConst<JPH::Shape> CreateShape(const FCollisionShape& Shape)
{
    constexpr float MinSize = 0.001f;
    switch (Shape.Type)
    {
    case ECollisionShapeType::Box:
        return new JPH::BoxShape(JPH::Vec3(
            std::max(Shape.Extent.X * PicoToJoltScale, MinSize),
            std::max(Shape.Extent.Y * PicoToJoltScale, MinSize),
            std::max(Shape.Extent.Z * PicoToJoltScale, MinSize)));
    case ECollisionShapeType::Sphere:
        return new JPH::SphereShape(std::max(Shape.Radius * PicoToJoltScale, MinSize));
    case ECollisionShapeType::Capsule:
    {
        const float Radius = std::max(Shape.Radius * PicoToJoltScale, MinSize);
        const float CylinderHalfHeight = std::max(
            (Shape.HalfHeight - Shape.Radius) * PicoToJoltScale,
            MinSize);
        JPH::RefConst<JPH::Shape> Capsule = new JPH::CapsuleShape(CylinderHalfHeight, Radius);
        return new JPH::RotatedTranslatedShape(
            JPH::Vec3::sZero(),
            JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::JPH_PI * 0.5f),
            Capsule);
    }
    case ECollisionShapeType::Point:
    default:
        return new JPH::SphereShape(MinSize);
    }
}

struct FBodyRecord
{
    JPH::BodyID BodyId;
    uint32 Serial = 0;
    FObjectHandle OwnerObject;
    EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
};

struct FCachedContact
{
    FObjectHandle A;
    FObjectHandle B;
    bool bSensor = false;
};

uint64 MakeContactKey(const JPH::BodyID& A, const JPH::BodyID& B)
{
    const uint32 Left = std::min(A.GetIndexAndSequenceNumber(), B.GetIndexAndSequenceNumber());
    const uint32 Right = std::max(A.GetIndexAndSequenceNumber(), B.GetIndexAndSequenceNumber());
    return (static_cast<uint64>(Left) << 32) | Right;
}
}

class FJoltPhysicsScene final : public IPhysicsScene, private JPH::ContactListener
{
public:
    FJoltPhysicsScene()
        : TempAllocator(8 * 1024 * 1024)
        , JobSystem(JPH::cMaxPhysicsJobs)
    {
        PhysicsSystem.Init(
            4096,
            0,
            8192,
            4096,
            BroadPhaseInterface,
            ObjectVsBroadPhaseFilter,
            ObjectLayerPairFilter);
        PhysicsSystem.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.81f));
        PhysicsSystem.SetContactListener(this);
        bValid = true;
    }

    ~FJoltPhysicsScene() override
    {
        PhysicsSystem.SetContactListener(nullptr);
        JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();
        for (const auto& [Id, Record] : BodyRecords)
        {
            (void)Id;
            if (!Record.BodyId.IsInvalid())
            {
                Bodies.RemoveBody(Record.BodyId);
                Bodies.DestroyBody(Record.BodyId);
            }
        }
        BodyRecords.clear();
        BodyFilters.clear();
        bValid = false;
    }

    bool IsValid() const override { return bValid; }

    uint32 Step(float DeltaSeconds) override
    {
        if (!bValid || !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
        {
            return 0;
        }
        AccumulatedTime = std::min(
            AccumulatedTime + DeltaSeconds,
            MaxAccumulatedPhysicsTime);
        uint32 SubstepCount = 0;
        while (AccumulatedTime + 0.000001f >= FixedPhysicsDeltaSeconds
            && SubstepCount < MaxPhysicsSubsteps)
        {
            PhysicsSystem.Update(
                FixedPhysicsDeltaSeconds,
                1,
                &TempAllocator,
                &JobSystem);
            AccumulatedTime -= FixedPhysicsDeltaSeconds;
            ++SubstepCount;
        }
        AccumulatedTime = std::max(AccumulatedTime, 0.0f);
        return SubstepCount;
    }

    FPhysicsBodyHandle CreateBody(const FPhysicsBodyDesc& Desc) override
    {
        if (!bValid || !Desc.Shape.IsValid()
            || Desc.CollisionEnabled == ECollisionEnabled::NoCollision)
        {
            return {};
        }

        JPH::RefConst<JPH::Shape> Shape = CreateShape(Desc.Shape);
        if (Shape == nullptr) return {};
        const JPH::EMotionType MotionType = Desc.BodyType == EPhysicsBodyType::Static
            ? JPH::EMotionType::Static
            : Desc.BodyType == EPhysicsBodyType::Kinematic
                ? JPH::EMotionType::Kinematic
                : JPH::EMotionType::Dynamic;
        const JPH::ObjectLayer Layer = Desc.BodyType == EPhysicsBodyType::Static
            ? Layers::NonMoving
            : Layers::Moving;
        JPH::BodyCreationSettings Settings(
            Shape,
            ToJoltPosition(Desc.Transform.Translation),
            ToJoltQuat(Desc.Transform.Rotation),
            MotionType,
            Layer);
        Settings.mUserData = EncodeObjectHandle(Desc.OwnerObject);
        Settings.mIsSensor = Desc.bSensor || Desc.CollisionEnabled == ECollisionEnabled::QueryOnly;
        Settings.mCollideKinematicVsNonDynamic =
            MotionType == JPH::EMotionType::Kinematic;
        Settings.mGravityFactor = Desc.bUseGravity ? 1.0f : 0.0f;
        if (MotionType == JPH::EMotionType::Dynamic && Desc.Mass > 0.0f)
        {
            Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            Settings.mMassPropertiesOverride.mMass = Desc.Mass;
        }

        JPH::BodyID BodyId = PhysicsSystem.GetBodyInterface().CreateAndAddBody(
            Settings,
            MotionType != JPH::EMotionType::Static
                ? JPH::EActivation::Activate
                : JPH::EActivation::DontActivate);
        if (BodyId.IsInvalid()) return {};

        FPhysicsBodyHandle Handle;
        Handle.Id = NextBodyHandle++;
        if (Handle.Id == 0) Handle.Id = NextBodyHandle++;
        Handle.Serial = NextBodySerial++;
        if (Handle.Serial == 0) Handle.Serial = NextBodySerial++;
        BodyRecords.emplace(Handle.Id, FBodyRecord { BodyId, Handle.Serial, Desc.OwnerObject, Desc.BodyType });
        BodyFilters[Settings.mUserData] = {
            Desc.CollisionFilter,
            Desc.CollisionEnabled,
            Desc.bSensor};
        return Handle;
    }

    void DestroyBody(FPhysicsBodyHandle Handle) override
    {
        FBodyRecord* Record = ResolveBody(Handle);
        if (Record == nullptr) return;
        JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();
        Bodies.RemoveBody(Record->BodyId);
        Bodies.DestroyBody(Record->BodyId);
        BodyFilters.erase(EncodeObjectHandle(Record->OwnerObject));
        BodyRecords.erase(Handle.Id);
    }

    bool SetBodyTransform(
        FPhysicsBodyHandle Handle,
        const FTransform& Transform,
        ETeleportType Teleport) override
    {
        FBodyRecord* Record = ResolveBody(Handle);
        if (Record == nullptr) return false;
        JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();
        Bodies.SetPositionAndRotation(
            Record->BodyId,
            ToJoltPosition(Transform.Translation),
            ToJoltQuat(Transform.Rotation),
            Record->BodyType == EPhysicsBodyType::Static
                ? JPH::EActivation::DontActivate
                : JPH::EActivation::Activate);
        if (Teleport == ETeleportType::ResetPhysics)
        {
            Bodies.SetLinearAndAngularVelocity(
                Record->BodyId,
                JPH::Vec3::sZero(),
                JPH::Vec3::sZero());
        }
        return true;
    }

    bool GetBodyState(FPhysicsBodyHandle Handle, FPhysicsBodyState& OutState) const override
    {
        const FBodyRecord* Record = ResolveBody(Handle);
        if (Record == nullptr) return false;
        const JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();
        JPH::RVec3 Position;
        JPH::Quat Rotation;
        Bodies.GetPositionAndRotation(Record->BodyId, Position, Rotation);
        OutState.Transform = FTransform(
            FromJoltQuat(Rotation),
            FromJoltPosition(Position),
            FVector3::OneVector);
        OutState.LinearVelocity = FromJoltVector(
            Bodies.GetLinearVelocity(Record->BodyId),
            JoltToPicoScale);
        OutState.AngularVelocity = FromJoltVector(
            Bodies.GetAngularVelocity(Record->BodyId));
        OutState.bActive = Bodies.IsActive(Record->BodyId);
        return true;
    }

    bool SetBodyState(
        FPhysicsBodyHandle Handle,
        const FPhysicsBodyState& State) override
    {
        FBodyRecord* Record = ResolveBody(Handle);
        if (Record == nullptr) return false;
        JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();
        Bodies.SetPositionAndRotation(
            Record->BodyId,
            ToJoltPosition(State.Transform.Translation),
            ToJoltQuat(State.Transform.Rotation),
            State.bActive
                ? JPH::EActivation::Activate
                : JPH::EActivation::DontActivate);
        Bodies.SetLinearAndAngularVelocity(
            Record->BodyId,
            ToJoltVector(State.LinearVelocity, PicoToJoltScale),
            ToJoltVector(State.AngularVelocity));
        if (State.bActive) Bodies.ActivateBody(Record->BodyId);
        else Bodies.DeactivateBody(Record->BodyId);
        return true;
    }

    bool AddImpulse(FPhysicsBodyHandle Handle, const FVector3& Impulse) override
    {
        FBodyRecord* Record = ResolveBody(Handle);
        if (Record == nullptr || Record->BodyType != EPhysicsBodyType::Dynamic) return false;
        PhysicsSystem.GetBodyInterface().AddImpulse(
            Record->BodyId,
            ToJoltVector(Impulse, PicoToJoltScale));
        return true;
    }

    bool Raycast(
        const FVector3& Start,
        const FVector3& End,
        const FCollisionQueryParams& Params,
        FHitResult& OutHit) const override
    {
        OutHit.Reset(Start, End);
        const FVector3 Delta = End - Start;
        if (Delta.IsNearlyZero()) return false;
        FQueryBodyFilter Filter(Params, BodyFilters, true);
        JPH::RRayCast Ray(ToJoltPosition(Start), ToJoltVector(Delta, PicoToJoltScale));
        JPH::RayCastResult Result;
        if (!PhysicsSystem.GetNarrowPhaseQuery().CastRay(Ray, Result, {}, {}, Filter)) return false;
        OutHit.bBlockingHit = true;
        OutHit.Time = std::clamp(Result.mFraction, 0.0f, 1.0f);
        OutHit.Distance = Delta.Size() * OutHit.Time;
        OutHit.Location = Start + Delta * OutHit.Time;
        OutHit.ImpactPoint = OutHit.Location;
        OutHit.HitObject = GetObjectHandle(Result.mBodyID);
        JPH::BodyLockRead BodyLock(
            PhysicsSystem.GetBodyLockInterface(),
            Result.mBodyID);
        if (BodyLock.Succeeded())
        {
            const JPH::Vec3 Normal = BodyLock.GetBody().GetWorldSpaceSurfaceNormal(
                Result.mSubShapeID2,
                Ray.GetPointOnRay(Result.mFraction));
            OutHit.Normal = FromJoltVector(Normal);
            OutHit.ImpactNormal = OutHit.Normal;
        }
        return true;
    }

    bool Sweep(
        const FCollisionShape& Shape,
        const FVector3& Start,
        const FVector3& End,
        const FQuat& Rotation,
        const FCollisionQueryParams& Params,
        FHitResult& OutHit) const override
    {
        OutHit.Reset(Start, End);
        JPH::RefConst<JPH::Shape> JoltShape = CreateShape(Shape);
        if (JoltShape == nullptr) return false;
        const FVector3 Delta = End - Start;
        FQueryBodyFilter Filter(Params, BodyFilters, true);
        JPH::RShapeCast ShapeCast(
            JoltShape,
            JPH::Vec3::sOne(),
            JPH::RMat44::sRotationTranslation(ToJoltQuat(Rotation), ToJoltPosition(Start)),
            ToJoltVector(Delta, PicoToJoltScale));
        JPH::ShapeCastSettings Settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> Collector;
        PhysicsSystem.GetNarrowPhaseQuery().CastShape(
            ShapeCast, Settings, JPH::RVec3::sZero(), Collector, {}, {}, Filter);
        if (!Collector.HadHit()) return false;
        const JPH::ShapeCastResult& Result = Collector.mHit;
        OutHit.bBlockingHit = true;
        OutHit.bStartPenetrating = Result.mFraction <= 0.0f;
        OutHit.Time = std::clamp(Result.mFraction, 0.0f, 1.0f);
        OutHit.Distance = Delta.Size() * OutHit.Time;
        OutHit.Location = Start + Delta * OutHit.Time;
        OutHit.ImpactPoint = FromJoltVector(Result.mContactPointOn2, JoltToPicoScale);
        OutHit.ImpactNormal = FromJoltVector(-Result.mPenetrationAxis.Normalized());
        OutHit.Normal = OutHit.ImpactNormal;
        OutHit.PenetrationDepth = Result.mPenetrationDepth * JoltToPicoScale;
        OutHit.HitObject = GetObjectHandle(Result.mBodyID2);
        return true;
    }

    bool Overlap(
        const FCollisionShape& Shape,
        const FVector3& Location,
        const FQuat& Rotation,
        const FCollisionQueryParams& Params,
        std::vector<FOverlapResult>& OutOverlaps) const override
    {
        OutOverlaps.clear();
        JPH::RefConst<JPH::Shape> JoltShape = CreateShape(Shape);
        if (JoltShape == nullptr) return false;
        FQueryBodyFilter Filter(Params, BodyFilters, false);
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> Collector;
        PhysicsSystem.GetNarrowPhaseQuery().CollideShape(
            JoltShape,
            JPH::Vec3::sOne(),
            JPH::RMat44::sRotationTranslation(ToJoltQuat(Rotation), ToJoltPosition(Location)),
            JPH::CollideShapeSettings(),
            JPH::RVec3::sZero(),
            Collector,
            {},
            {},
            Filter);
        for (const JPH::CollideShapeResult& Hit : Collector.mHits)
        {
            const FObjectHandle Handle = GetObjectHandle(Hit.mBodyID2);
            if (Handle.IsValid()
                && std::none_of(OutOverlaps.begin(), OutOverlaps.end(),
                    [Handle](const FOverlapResult& Item) { return Item.OverlapObject == Handle; }))
            {
                OutOverlaps.push_back({Handle});
            }
        }
        return !OutOverlaps.empty();
    }

    std::vector<FPhysicsContactEvent> DrainContactEvents() override
    {
        std::vector<FPhysicsContactEvent> Result = std::move(PendingContactEvents);
        PendingContactEvents.clear();
        return Result;
    }

private:
    ECollisionResponse GetBodyPairResponse(
        const JPH::Body& Body1,
        const JPH::Body& Body2) const
    {
        const auto Left = BodyFilters.find(Body1.GetUserData());
        const auto Right = BodyFilters.find(Body2.GetUserData());
        return Left != BodyFilters.end() && Right != BodyFilters.end()
            ? ResolveCollisionResponse(
                Left->second.Filter, Right->second.Filter)
            : ECollisionResponse::Block;
    }

    bool ShouldCreateBodyPairContact(
        const JPH::Body& Body1,
        const JPH::Body& Body2,
        ECollisionResponse Response) const
    {
        const auto Left = BodyFilters.find(Body1.GetUserData());
        const auto Right = BodyFilters.find(Body2.GetUserData());
        if (Left == BodyFilters.end() || Right == BodyFilters.end()) return true;
        if (Left->second.bExplicitSensor || Right->second.bExplicitSensor
            || Response == ECollisionResponse::Overlap)
        {
            return true;
        }
        return HasPhysicsCollision(Left->second.CollisionEnabled)
            && HasPhysicsCollision(Right->second.CollisionEnabled);
    }

    FBodyRecord* ResolveBody(FPhysicsBodyHandle Handle)
    {
        const auto Found = BodyRecords.find(Handle.Id);
        return Found != BodyRecords.end() && Found->second.Serial == Handle.Serial
            ? &Found->second
            : nullptr;
    }

    const FBodyRecord* ResolveBody(FPhysicsBodyHandle Handle) const
    {
        const auto Found = BodyRecords.find(Handle.Id);
        return Found != BodyRecords.end() && Found->second.Serial == Handle.Serial
            ? &Found->second
            : nullptr;
    }

    FObjectHandle GetObjectHandle(const JPH::BodyID& BodyId) const
    {
        return BodyId.IsInvalid()
            ? FObjectHandle {}
            : DecodeObjectHandle(PhysicsSystem.GetBodyInterface().GetUserData(BodyId));
    }

    void QueueContact(
        EPhysicsContactEventType Type,
        const JPH::Body& Body1,
        const JPH::Body& Body2,
        const JPH::ContactManifold& Manifold,
        bool bSensor)
    {
        FPhysicsContactEvent Event;
        Event.Type = Type;
        Event.ObjectA = DecodeObjectHandle(Body1.GetUserData());
        Event.ObjectB = DecodeObjectHandle(Body2.GetUserData());
        Event.bSensor = bSensor;
        Event.Normal = FromJoltVector(Manifold.mWorldSpaceNormal);
        if (!Manifold.mRelativeContactPointsOn1.empty())
        {
            Event.Location = FromJoltPosition(Manifold.GetWorldSpaceContactPointOn1(0));
        }
        PendingContactEvents.push_back(Event);
        ContactCache[MakeContactKey(Body1.GetID(), Body2.GetID())] = {
            Event.ObjectA, Event.ObjectB, Event.bSensor };
    }

    JPH::ValidateResult OnContactValidate(
        const JPH::Body& Body1,
        const JPH::Body& Body2,
        JPH::RVec3Arg,
        const JPH::CollideShapeResult&) override
    {
        const ECollisionResponse Response = GetBodyPairResponse(Body1, Body2);
        return Response == ECollisionResponse::Ignore
                || !ShouldCreateBodyPairContact(Body1, Body2, Response)
            ? JPH::ValidateResult::RejectAllContactsForThisBodyPair
            : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(
        const JPH::Body& Body1,
        const JPH::Body& Body2,
        const JPH::ContactManifold& Manifold,
        JPH::ContactSettings& Settings) override
    {
        Settings.mIsSensor = Settings.mIsSensor
            || GetBodyPairResponse(Body1, Body2)
                == ECollisionResponse::Overlap;
        QueueContact(EPhysicsContactEventType::Begin,
            Body1, Body2, Manifold, Settings.mIsSensor);
    }

    void OnContactPersisted(
        const JPH::Body& Body1,
        const JPH::Body& Body2,
        const JPH::ContactManifold& Manifold,
        JPH::ContactSettings& Settings) override
    {
        Settings.mIsSensor = Settings.mIsSensor
            || GetBodyPairResponse(Body1, Body2)
                == ECollisionResponse::Overlap;
        QueueContact(EPhysicsContactEventType::Persist,
            Body1, Body2, Manifold, Settings.mIsSensor);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& Pair) override
    {
        const uint64 Key = MakeContactKey(Pair.GetBody1ID(), Pair.GetBody2ID());
        const auto Found = ContactCache.find(Key);
        if (Found == ContactCache.end()) return;
        FPhysicsContactEvent Event;
        Event.Type = EPhysicsContactEventType::End;
        Event.ObjectA = Found->second.A;
        Event.ObjectB = Found->second.B;
        Event.bSensor = Found->second.bSensor;
        PendingContactEvents.push_back(Event);
        ContactCache.erase(Found);
    }

    FJoltRuntimeGuard RuntimeGuard;
    FBroadPhaseLayerInterface BroadPhaseInterface;
    FObjectVsBroadPhaseFilter ObjectVsBroadPhaseFilter;
    FObjectLayerPairFilter ObjectLayerPairFilter;
    JPH::PhysicsSystem PhysicsSystem;
    JPH::TempAllocatorImpl TempAllocator;
    JPH::JobSystemSingleThreaded JobSystem;
    std::unordered_map<uint32, FBodyRecord> BodyRecords;
    FQueryBodyFilter::FFilterMap BodyFilters;
    std::unordered_map<uint64, FCachedContact> ContactCache;
    std::vector<FPhysicsContactEvent> PendingContactEvents;
    uint32 NextBodyHandle = 1;
    uint32 NextBodySerial = 1;
    float AccumulatedTime = 0.0f;
    bool bValid = false;
};

std::unique_ptr<IPhysicsScene> CreateJoltPhysicsScene()
{
    auto Scene = std::make_unique<FJoltPhysicsScene>();
    return Scene->IsValid() ? std::move(Scene) : nullptr;
}

const char* GetJoltPhysicsVersion()
{
    return "5.6.0";
}
}
