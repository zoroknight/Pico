#include "Pico/Engine/CharacterMovementComponent.h"

#include "Pico/Core/Log.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/Controller.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Net/NetPacket.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PCharacterMovementComponent)

namespace
{
constexpr float GravityZ = -980.0f;

FVector3 MoveToward(const FVector3& Current, const FVector3& Target, float MaxDelta)
{
    const FVector3 Difference = Target - Current;
    const float Distance = Difference.Size();
    if (Distance <= MaxDelta || Distance <= SmallNumber) return Target;
    return Current + Difference * (MaxDelta / Distance);
}
}

bool PCharacterMovementComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, MaxWalkSpeed);
    PICO_ADD_PROPERTY(Properties, GroundAcceleration);
    PICO_ADD_PROPERTY(Properties, BrakingDeceleration);
    PICO_ADD_PROPERTY(Properties, AirControl);
    PICO_ADD_PROPERTY(Properties, GravityScale);
    PICO_ADD_PROPERTY(Properties, JumpZVelocity);
    PICO_ADD_PROPERTY(Properties, WalkableFloorAngle);
    PICO_ADD_PROPERTY(Properties, FloorProbeDistance);
    PICO_ADD_PROPERTY(Properties, MaxSimulationDeltaTime);
    PICO_ADD_PROPERTY(Properties, MaxSimulationIterations);
    PICO_ADD_PROPERTY(Properties, PushImpulse);
    FPropertyMetadata PhysicsInteractionMetadata;
    PhysicsInteractionMetadata.DisplayName = "Enable Physics Interaction";
    PhysicsInteractionMetadata.Description =
        "Allow authoritative Character movement to push Dynamic physics bodies";
    PICO_ADD_PROPERTY_METADATA(
        Properties, bEnablePhysicsInteraction, PhysicsInteractionMetadata);
    PICO_ADD_PROPERTY(Properties, bOrientRotationToMovement);
    PICO_ADD_PROPERTY(Properties, bUseControllerDesiredRotation);
    PICO_ADD_PROPERTY(Properties, RotationRate);
    PICO_ADD_PROPERTY(Properties, NetworkSimulatedSmoothLocationTime);
    PICO_ADD_PROPERTY(Properties, bUseAdaptiveNetworkSmoothing);
    PICO_ADD_PROPERTY(Properties, NetworkMinAdaptiveSmoothTime);
    PICO_ADD_PROPERTY(Properties, NetworkMaxAdaptiveSmoothTime);
    PICO_ADD_PROPERTY(Properties, NetworkMaxSmoothUpdateDistance);
    PICO_ADD_PROPERTY(Properties, NetworkNoSmoothUpdateDistance);
    PICO_ADD_PROPERTY(Properties, SnapshotInterpolationDelayTicks);
    PICO_ADD_PROPERTY(Properties, bEnableSimulatedProxyExtrapolation);
    PICO_ADD_PROPERTY(Properties, NetworkMaxSimulatedProxyExtrapolationTime);
    FPropertyMetadata SmoothingMetadata;
    SmoothingMetadata.DisplayName = "Network Smoothing Mode";
    SmoothingMetadata.EnumOptions = {
        {static_cast<int32>(ENetworkSmoothingMode::Disabled), "Disabled"},
        {static_cast<int32>(ENetworkSmoothingMode::Linear), "Linear"},
        {static_cast<int32>(ENetworkSmoothingMode::Exponential), "Exponential"},
        {static_cast<int32>(ENetworkSmoothingMode::SnapshotInterpolation),
            "Snapshot Interpolation"}
    };
    PICO_ADD_PROPERTY_METADATA(
        Properties, NetworkSmoothingModeValue, SmoothingMetadata);
    FPropertyMetadata RuntimeMetadata;
    RuntimeMetadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    RuntimeMetadata.DisplayName = "Movement Mode";
    RuntimeMetadata.EnumOptions = {
        {static_cast<int32>(EMovementMode::None), "None"},
        {static_cast<int32>(EMovementMode::Walking), "Walking"},
        {static_cast<int32>(EMovementMode::Falling), "Falling"}
    };
    PICO_ADD_PROPERTY_METADATA(Properties, MovementModeValue, RuntimeMetadata);
    return Class.AddProperties(std::move(Properties));
}

const char* ToString(EMovementMode Mode)
{
    switch (Mode)
    {
    case EMovementMode::None: return "None";
    case EMovementMode::Walking: return "Walking";
    case EMovementMode::Falling: return "Falling";
    }
    return "Unknown";
}

const char* ToString(ENetworkSmoothingMode Mode)
{
    switch (Mode)
    {
    case ENetworkSmoothingMode::Disabled: return "Disabled";
    case ENetworkSmoothingMode::Linear: return "Linear";
    case ENetworkSmoothingMode::Exponential: return "Exponential";
    case ENetworkSmoothingMode::SnapshotInterpolation:
        return "Snapshot Interpolation";
    }
    return "Unknown";
}

PCharacterMovementComponent::PCharacterMovementComponent(
    const FObjectConstructionParams& Params)
    : PPawnMovementComponent(Params)
{
}

PCharacter* PCharacterMovementComponent::GetCharacterOwner() const
{
    PPawn* Pawn = GetPawnOwner();
    return Pawn != nullptr && Pawn->IsA(PCharacter::StaticClass())
        ? static_cast<PCharacter*>(Pawn)
        : nullptr;
}

EMovementMode PCharacterMovementComponent::GetMovementMode() const
{
    const int32 Minimum = static_cast<int32>(EMovementMode::None);
    const int32 Maximum = static_cast<int32>(EMovementMode::Falling);
    return static_cast<EMovementMode>(std::clamp(MovementModeValue, Minimum, Maximum));
}

bool PCharacterMovementComponent::IsMovingOnGround() const
{
    return GetMovementMode() == EMovementMode::Walking;
}

void PCharacterMovementComponent::SetMovementMode(EMovementMode Mode)
{
    MovementModeValue = static_cast<int32>(Mode);
}

const FFindFloorResult& PCharacterMovementComponent::GetCurrentFloor() const
{
    return CurrentFloor;
}

int32 PCharacterMovementComponent::GetLastSimulationIterations() const
{
    return LastSimulationIterations;
}

const FCharacterMoveInput& PCharacterMovementComponent::GetLastSimulationInput() const
{
    return LastSimulationInput;
}

float PCharacterMovementComponent::GetMaxWalkSpeed() const { return MaxWalkSpeed; }
void PCharacterMovementComponent::SetMaxWalkSpeed(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) MaxWalkSpeed = Value;
}
float PCharacterMovementComponent::GetGroundAcceleration() const { return GroundAcceleration; }
void PCharacterMovementComponent::SetGroundAcceleration(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) GroundAcceleration = Value;
}
float PCharacterMovementComponent::GetBrakingDeceleration() const { return BrakingDeceleration; }
void PCharacterMovementComponent::SetBrakingDeceleration(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) BrakingDeceleration = Value;
}
float PCharacterMovementComponent::GetAirControl() const { return AirControl; }
void PCharacterMovementComponent::SetAirControl(float Value)
{
    if (std::isfinite(Value)) AirControl = std::clamp(Value, 0.0f, 1.0f);
}
float PCharacterMovementComponent::GetGravityScale() const { return GravityScale; }
void PCharacterMovementComponent::SetGravityScale(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) GravityScale = Value;
}
float PCharacterMovementComponent::GetJumpZVelocity() const { return JumpZVelocity; }
void PCharacterMovementComponent::SetJumpZVelocity(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) JumpZVelocity = Value;
}
float PCharacterMovementComponent::GetWalkableFloorAngle() const { return WalkableFloorAngle; }
void PCharacterMovementComponent::SetWalkableFloorAngle(float Value)
{
    if (std::isfinite(Value)) WalkableFloorAngle = std::clamp(Value, 0.0f, 89.0f);
}
float PCharacterMovementComponent::GetMaxSimulationDeltaTime() const { return MaxSimulationDeltaTime; }
void PCharacterMovementComponent::SetMaxSimulationDeltaTime(float Value)
{
    if (std::isfinite(Value) && Value > 0.0f) MaxSimulationDeltaTime = Value;
}
int32 PCharacterMovementComponent::GetMaxSimulationIterations() const { return MaxSimulationIterations; }
void PCharacterMovementComponent::SetMaxSimulationIterations(int32 Value)
{
    MaxSimulationIterations = std::clamp(Value, 1, 16);
}
float PCharacterMovementComponent::GetPushImpulse() const { return PushImpulse; }
void PCharacterMovementComponent::SetPushImpulse(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f) PushImpulse = Value;
}
bool PCharacterMovementComponent::IsPhysicsInteractionEnabled() const
{
    return bEnablePhysicsInteraction;
}
void PCharacterMovementComponent::SetPhysicsInteractionEnabled(bool bValue)
{
    bEnablePhysicsInteraction = bValue;
}
bool PCharacterMovementComponent::ShouldOrientRotationToMovement() const
{ return bOrientRotationToMovement; }
void PCharacterMovementComponent::SetOrientRotationToMovement(bool bValue)
{ bOrientRotationToMovement = bValue; }
bool PCharacterMovementComponent::UsesControllerDesiredRotation() const
{ return bUseControllerDesiredRotation; }
void PCharacterMovementComponent::SetUseControllerDesiredRotation(bool bValue)
{ bUseControllerDesiredRotation = bValue; }
float PCharacterMovementComponent::GetRotationRate() const { return RotationRate; }
void PCharacterMovementComponent::SetRotationRate(float Value)
{ if (std::isfinite(Value) && Value >= 0.0f) RotationRate = Value; }
ENetworkSmoothingMode PCharacterMovementComponent::GetNetworkSmoothingMode() const
{
    const int32 Minimum = static_cast<int32>(ENetworkSmoothingMode::Disabled);
    const int32 Maximum = static_cast<int32>(
        ENetworkSmoothingMode::SnapshotInterpolation);
    return static_cast<ENetworkSmoothingMode>(
        std::clamp(NetworkSmoothingModeValue, Minimum, Maximum));
}
void PCharacterMovementComponent::SetNetworkSmoothingMode(
    ENetworkSmoothingMode Mode)
{
    ClearNetworkMeshSmoothing();
    SimulatedSnapshots.clear();
    SimulatedPlaybackTick = 0.0f;
    NetworkSmoothingModeValue = static_cast<int32>(Mode);
    ActiveNetworkSmoothingMode = Mode;
}
float PCharacterMovementComponent::GetNetworkSimulatedSmoothLocationTime() const
{ return NetworkSimulatedSmoothLocationTime; }
void PCharacterMovementComponent::SetNetworkSimulatedSmoothLocationTime(float Value)
{
    if (std::isfinite(Value))
    {
        NetworkSimulatedSmoothLocationTime = std::clamp(Value, 0.001f, 1.0f);
        if (!bUseAdaptiveNetworkSmoothing)
            EffectiveNetworkSmoothingTimeSeconds =
                NetworkSimulatedSmoothLocationTime;
    }
}
bool PCharacterMovementComponent::UsesAdaptiveNetworkSmoothing() const
{ return bUseAdaptiveNetworkSmoothing; }
void PCharacterMovementComponent::SetUseAdaptiveNetworkSmoothing(bool bValue)
{
    bUseAdaptiveNetworkSmoothing = bValue;
    if (!bValue)
        EffectiveNetworkSmoothingTimeSeconds =
            NetworkSimulatedSmoothLocationTime;
}
float PCharacterMovementComponent::GetNetworkMinAdaptiveSmoothTime() const
{ return NetworkMinAdaptiveSmoothTime; }
void PCharacterMovementComponent::SetNetworkMinAdaptiveSmoothTime(float Value)
{
    if (!std::isfinite(Value)) return;
    NetworkMinAdaptiveSmoothTime = std::clamp(Value, 0.001f, 1.0f);
    NetworkMaxAdaptiveSmoothTime = std::max(
        NetworkMaxAdaptiveSmoothTime, NetworkMinAdaptiveSmoothTime);
}
float PCharacterMovementComponent::GetNetworkMaxAdaptiveSmoothTime() const
{ return NetworkMaxAdaptiveSmoothTime; }
void PCharacterMovementComponent::SetNetworkMaxAdaptiveSmoothTime(float Value)
{
    if (!std::isfinite(Value)) return;
    NetworkMaxAdaptiveSmoothTime = std::clamp(
        Value, NetworkMinAdaptiveSmoothTime, 1.0f);
}
float PCharacterMovementComponent::GetEffectiveNetworkSmoothingTime() const
{
    return bUseAdaptiveNetworkSmoothing
        ? EffectiveNetworkSmoothingTimeSeconds
        : NetworkSimulatedSmoothLocationTime;
}
float PCharacterMovementComponent::GetNetworkMaxSmoothUpdateDistance() const
{ return NetworkMaxSmoothUpdateDistance; }
void PCharacterMovementComponent::SetNetworkMaxSmoothUpdateDistance(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f)
    {
        NetworkMaxSmoothUpdateDistance = Value;
        NetworkNoSmoothUpdateDistance = std::max(
            NetworkNoSmoothUpdateDistance, NetworkMaxSmoothUpdateDistance);
    }
}
float PCharacterMovementComponent::GetNetworkNoSmoothUpdateDistance() const
{ return NetworkNoSmoothUpdateDistance; }
void PCharacterMovementComponent::SetNetworkNoSmoothUpdateDistance(float Value)
{
    if (std::isfinite(Value) && Value >= 0.0f)
        NetworkNoSmoothUpdateDistance = std::max(
            Value, NetworkMaxSmoothUpdateDistance);
}
float PCharacterMovementComponent::GetSnapshotInterpolationDelayTicks() const
{ return SnapshotInterpolationDelayTicks; }
void PCharacterMovementComponent::SetSnapshotInterpolationDelayTicks(float Value)
{
    if (std::isfinite(Value))
        SnapshotInterpolationDelayTicks = std::clamp(Value, 0.0f, 8.0f);
}
bool PCharacterMovementComponent::IsSimulatedProxyExtrapolationEnabled() const
{ return bEnableSimulatedProxyExtrapolation; }
void PCharacterMovementComponent::SetSimulatedProxyExtrapolationEnabled(bool bValue)
{ bEnableSimulatedProxyExtrapolation = bValue; }
float PCharacterMovementComponent::GetNetworkMaxSimulatedProxyExtrapolationTime() const
{ return NetworkMaxSimulatedProxyExtrapolationTime; }
void PCharacterMovementComponent::SetNetworkMaxSimulatedProxyExtrapolationTime(
    float Value)
{
    if (std::isfinite(Value))
        NetworkMaxSimulatedProxyExtrapolationTime = std::clamp(Value, 0.0f, 1.0f);
}
float PCharacterMovementComponent::GetNetworkSmoothingVisualOffsetDistance() const
{ return NetworkSmoothingVisualOffsetDistance; }

FCharacterMoveState PCharacterMovementComponent::CaptureMoveState() const
{
    FCharacterMoveState State;
    if (const PSceneComponent* Component = GetUpdatedComponent())
    {
        State.Transform = Component->GetWorldTransform();
    }
    State.Velocity = GetVelocity();
    State.MovementMode = GetMovementMode();
    return State;
}

bool PCharacterMovementComponent::ApplyMoveState(const FCharacterMoveState& State)
{
    PSceneComponent* Component = GetUpdatedComponent();
    if (Component == nullptr
        || !std::isfinite(State.Velocity.X)
        || !std::isfinite(State.Velocity.Y)
        || !std::isfinite(State.Velocity.Z))
    {
        return false;
    }
    Component->SetWorldTransform(State.Transform);
    SetVelocity(State.Velocity);
    SetMovementMode(State.MovementMode);
    FindFloor(CurrentFloor);
    return true;
}

bool PCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
    const float WalkableFloorZ = std::cos(WalkableFloorAngle * Pico::Pi / 180.0f);
    return Hit.bBlockingHit && Hit.ImpactNormal.GetSafeNormal().Z >= WalkableFloorZ;
}

bool PCharacterMovementComponent::FindFloor(FFindFloorResult& OutFloor) const
{
    OutFloor = {};
    const PSceneComponent* Component = GetUpdatedComponent();
    PWorld* World = GetWorld();
    IWorldCollisionQuery* Query = World != nullptr ? World->GetCollisionQuery() : nullptr;
    if (Component == nullptr || Query == nullptr
        || (Component->IsA(PPrimitiveComponent::StaticClass())
            && !HasQueryCollision(
                static_cast<const PPrimitiveComponent*>(Component)
                    ->GetCollisionEnabled())))
    {
        return false;
    }

    const FTransform Transform = Component->GetWorldTransform();
    FCollisionQueryParams Params;
    Params.MovingObject = Component->GetHandle();
    FHitResult Hit;
    const FVector3 End = Transform.Translation
        - FVector3::UpVector * std::max(FloorProbeDistance, 0.5f);
    if (!Query->Sweep(
            Component->GetCollisionShape(),
            Transform.Translation,
            End,
            Transform.Rotation,
            Params,
            Hit))
    {
        return false;
    }

    OutFloor.HitResult = Hit;
    OutFloor.FloorDistance = std::max(FloorProbeDistance, 0.5f) * Hit.Time;
    OutFloor.bBlockingHit = Hit.bBlockingHit;
    OutFloor.bWalkableFloor = IsWalkable(Hit);
    return OutFloor.bBlockingHit;
}

void PCharacterMovementComponent::HandleImpact(
    const FHitResult& Hit,
    const FVector3& MoveDelta)
{
    if (!bEnablePhysicsInteraction || !Hit.bBlockingHit
        || PushImpulse <= 0.0f)
    {
        return;
    }
    PCharacter* Character = GetCharacterOwner();
    PWorld* World = Character != nullptr ? Character->GetWorld() : nullptr;
    FNetDriver* Driver = World != nullptr ? World->GetNetDriver() : nullptr;
    if (Driver != nullptr && Driver->GetNetMode() != ENetMode::Standalone
        && Character->GetLocalRole() == ENetRole::SimulatedProxy)
    {
        return;
    }
    PObject* Object = ResolveObject(Hit.HitObject);
    if (Object == nullptr || !Object->IsA(PPrimitiveComponent::StaticClass())) return;
    auto* Primitive = static_cast<PPrimitiveComponent*>(Object);
    if (Primitive->GetPhysicsBodyType() != EPhysicsBodyType::Dynamic) return;

    FVector3 Direction(MoveDelta.X, MoveDelta.Y, 0.0f);
    if (Direction.IsNearlyZero()) Direction = -Hit.ImpactNormal;
    Direction.Z = 0.0f;
    if (!Direction.IsNearlyZero()) Primitive->AddImpulse(Direction.GetSafeNormal() * PushImpulse);
}

void PCharacterMovementComponent::SimulateWalking(
    const FCharacterMoveInput& Input,
    float DeltaSeconds)
{
    FVector3 DesiredInput(Input.WorldInput.X, Input.WorldInput.Y, 0.0f);
    if (DesiredInput.Size() > 1.0f) DesiredInput = DesiredInput.GetSafeNormal();
    if (CurrentFloor.bWalkableFloor && !DesiredInput.IsNearlyZero())
    {
        const FVector3 Normal = CurrentFloor.HitResult.ImpactNormal.GetSafeNormal();
        DesiredInput -= Normal * FVector3::Dot(DesiredInput, Normal);
        DesiredInput = DesiredInput.GetSafeNormal();
    }

    FVector3 NewVelocity = GetVelocity();
    NewVelocity.Z = 0.0f;
    NewVelocity = MoveToward(
        NewVelocity,
        DesiredInput * MaxWalkSpeed,
        (DesiredInput.IsNearlyZero() ? BrakingDeceleration : GroundAcceleration)
            * DeltaSeconds);
    SetVelocity(NewVelocity);

    const FVector3 Delta = NewVelocity * DeltaSeconds;
    FHitResult Hit;
    SafeMoveUpdatedComponent(
        Delta,
        GetUpdatedComponent()->GetWorldTransform().Rotation,
        true,
        &Hit);
    if (Hit.bBlockingHit)
    {
        HandleImpact(Hit, Delta);
        const FHitResult InitialHit = Hit;
        SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
        const FVector3 Normal = InitialHit.Normal.GetSafeNormal();
        SetVelocity(NewVelocity - Normal * FVector3::Dot(NewVelocity, Normal));
    }

    if (!FindFloor(CurrentFloor) || !CurrentFloor.bWalkableFloor)
    {
        SetMovementMode(EMovementMode::Falling);
        return;
    }
    if (CurrentFloor.FloorDistance > 0.5f)
    {
        FHitResult FloorHit;
        SafeMoveUpdatedComponent(
            -FVector3::UpVector * CurrentFloor.FloorDistance,
            GetUpdatedComponent()->GetWorldTransform().Rotation,
            true,
            &FloorHit);
    }
}

void PCharacterMovementComponent::SimulateFalling(
    const FCharacterMoveInput& Input,
    float DeltaSeconds)
{
    FVector3 HorizontalInput(Input.WorldInput.X, Input.WorldInput.Y, 0.0f);
    if (HorizontalInput.Size() > 1.0f) HorizontalInput = HorizontalInput.GetSafeNormal();

    FVector3 NewVelocity = GetVelocity();
    const FVector3 CurrentHorizontal(NewVelocity.X, NewVelocity.Y, 0.0f);
    const FVector3 TargetHorizontal = HorizontalInput * MaxWalkSpeed;
    const FVector3 NewHorizontal = MoveToward(
        CurrentHorizontal,
        TargetHorizontal,
        GroundAcceleration * AirControl * DeltaSeconds);
    NewVelocity.X = NewHorizontal.X;
    NewVelocity.Y = NewHorizontal.Y;
    NewVelocity.Z += GravityZ * GravityScale * DeltaSeconds;
    SetVelocity(NewVelocity);

    const FVector3 Delta = NewVelocity * DeltaSeconds;
    FHitResult Hit;
    SafeMoveUpdatedComponent(
        Delta,
        GetUpdatedComponent()->GetWorldTransform().Rotation,
        true,
        &Hit);
    if (Hit.bBlockingHit)
    {
        HandleImpact(Hit, Delta);
        if (NewVelocity.Z <= 0.0f && IsWalkable(Hit))
        {
            NewVelocity.Z = 0.0f;
            SetVelocity(NewVelocity);
            SetMovementMode(EMovementMode::Walking);
            FindFloor(CurrentFloor);
            return;
        }

        const FVector3 Normal = Hit.Normal.GetSafeNormal();
        SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
        SetVelocity(NewVelocity - Normal * FVector3::Dot(NewVelocity, Normal));
    }
    FindFloor(CurrentFloor);
}

void PCharacterMovementComponent::SimulateStep(
    const FCharacterMoveInput& Input,
    float DeltaSeconds)
{
    if (GetMovementMode() == EMovementMode::None) return;

    if (GetMovementMode() == EMovementMode::Falling
        && GetVelocity().Z <= 0.0f
        && FindFloor(CurrentFloor)
        && CurrentFloor.bWalkableFloor
        && CurrentFloor.FloorDistance <= FloorProbeDistance)
    {
        FVector3 NewVelocity = GetVelocity();
        NewVelocity.Z = 0.0f;
        SetVelocity(NewVelocity);
        SetMovementMode(EMovementMode::Walking);
    }

    if (Input.bJumpPressed && GetMovementMode() == EMovementMode::Walking)
    {
        FVector3 NewVelocity = GetVelocity();
        NewVelocity.Z = JumpZVelocity;
        SetVelocity(NewVelocity);
        SetMovementMode(EMovementMode::Falling);
        CurrentFloor = {};
    }

    if (GetMovementMode() == EMovementMode::Walking)
        SimulateWalking(Input, DeltaSeconds);
    else if (GetMovementMode() == EMovementMode::Falling)
        SimulateFalling(Input, DeltaSeconds);
}

void PCharacterMovementComponent::SimulateMovement(
    const FCharacterMoveInput& Input,
    float DeltaSeconds)
{
    LastSimulationInput = Input;
    LastSimulationIterations = 0;
    if (GetUpdatedComponent() == nullptr
        || !std::isfinite(DeltaSeconds)
        || DeltaSeconds <= 0.0f
        || !std::isfinite(Input.WorldInput.X)
        || !std::isfinite(Input.WorldInput.Y)
        || !std::isfinite(Input.WorldInput.Z))
    {
        return;
    }

    if (!Input.RootMotionDelta.Equals(FTransform::Identity))
    {
        FHitResult Hit;
        PSceneComponent* Updated = GetUpdatedComponent();
        const FQuat NewRotation = Updated->GetWorldTransform().Rotation * Input.RootMotionDelta.Rotation;
        Updated->MoveComponent(
            Input.RootMotionDelta.Translation,
            NewRotation,
            true,
            &Hit,
            EMoveComponentFlags::None,
            ETeleportType::None);
        if (Hit.bBlockingHit)
        {
            HandleImpact(Hit, Input.RootMotionDelta.Translation);
            SlideAlongSurface(
                Input.RootMotionDelta.Translation,
                1.0f - Hit.Time,
                Hit.Normal,
                Hit,
                true);
        }
    }

    float RemainingTime = std::min(
        DeltaSeconds,
        MaxSimulationDeltaTime * static_cast<float>(MaxSimulationIterations));
    bool bJumpAvailable = Input.bJumpPressed;
    while (RemainingTime > SmallNumber
        && LastSimulationIterations < MaxSimulationIterations)
    {
        const float StepDelta = std::min(RemainingTime, MaxSimulationDeltaTime);
        FCharacterMoveInput StepInput = Input;
        StepInput.bJumpPressed = bJumpAvailable;
        SimulateStep(StepInput, StepDelta);
        bJumpAvailable = false;
        RemainingTime -= StepDelta;
        ++LastSimulationIterations;
    }

    PCharacter* Character = GetCharacterOwner();
    if (Character != nullptr)
    {
        FVector3 Facing = Input.WorldInput;
        Facing.Z = 0.0f;
        FRotator Rotation = Character->GetActorRotation();
        PController* Controller = Character->GetController();
        if (Character->UsesControllerRotationYaw() && Controller != nullptr)
        {
            Rotation.Yaw = Controller->GetControlRotation().Yaw;
            Character->SetActorRotation(Rotation);
        }
        else
        {
            bool bHasTargetYaw = false;
            float TargetYaw = Rotation.Yaw;
            if (bOrientRotationToMovement && !Facing.IsNearlyZero())
            {
                TargetYaw = std::atan2(Facing.Y, Facing.X) * 180.0f / Pico::Pi;
                bHasTargetYaw = true;
            }
            else if (bUseControllerDesiredRotation && Controller != nullptr)
            {
                TargetYaw = Controller->GetControlRotation().Yaw;
                bHasTargetYaw = true;
            }
            if (bHasTargetYaw)
            {
                const float DeltaYaw = FRotator::NormalizeAxis(TargetYaw - Rotation.Yaw);
                Rotation.Yaw += std::clamp(
                    DeltaYaw, -RotationRate * DeltaSeconds, RotationRate * DeltaSeconds);
                Character->SetActorRotation(Rotation);
            }
        }
    }
}

void PCharacterMovementComponent::QueueRootMotion(const FTransform& Delta)
{
    PendingRootMotion = PendingRootMotion * Delta;
}

namespace
{
constexpr std::size_t MaxPendingNetworkMoves = 32;
constexpr std::size_t MaxServerNetworkMoves = 32;
constexpr std::size_t MaxSimulatedSnapshots = 32;
constexpr std::size_t MaxMovesPerServerTick = 8;
constexpr std::size_t MovesPerPacket = 3;
constexpr float NetworkCorrectionDistance = 2.0f;
constexpr float NetworkCorrectionVelocity = 5.0f;
constexpr float NetworkSimulationRate = 60.0f;

bool IsAcknowledged(uint32 Sequence, uint32 Acknowledged)
{
    return Sequence == Acknowledged
        || !IsNetSequenceNewer(Sequence, Acknowledged);
}

FVector3 LerpVector(const FVector3& A, const FVector3& B, float Alpha)
{
    return A + (B - A) * Alpha;
}

FRotator LerpRotation(const FRotator& A, const FRotator& B, float Alpha)
{
    return FRotator(
        A.Pitch + FRotator::NormalizeAxis(B.Pitch - A.Pitch) * Alpha,
        A.Yaw + FRotator::NormalizeAxis(B.Yaw - A.Yaw) * Alpha,
        A.Roll + FRotator::NormalizeAxis(B.Roll - A.Roll) * Alpha);
}

FCharacterMoveState InterpolateState(
    const FCharacterMoveState& A,
    const FCharacterMoveState& B,
    float Alpha)
{
    FCharacterMoveState Result;
    Result.Transform.Translation = LerpVector(
        A.Transform.Translation, B.Transform.Translation, Alpha);
    Result.Transform.Scale = LerpVector(
        A.Transform.Scale, B.Transform.Scale, Alpha);
    Result.Transform.Rotation = LerpRotation(
        A.Transform.Rotation.Rotator(),
        B.Transform.Rotation.Rotator(), Alpha).Quaternion();
    Result.Velocity = LerpVector(A.Velocity, B.Velocity, Alpha);
    Result.MovementMode = Alpha < 0.5f ? A.MovementMode : B.MovementMode;
    return Result;
}

FTransform InterpolateTransform(
    const FTransform& A,
    const FTransform& B,
    float Alpha)
{
    FTransform Result;
    Result.Translation = LerpVector(A.Translation, B.Translation, Alpha);
    Result.Scale = LerpVector(A.Scale, B.Scale, Alpha);
    Result.Rotation = LerpRotation(
        A.Rotation.Rotator(), B.Rotation.Rotator(), Alpha).Quaternion();
    return Result;
}
}

void PCharacterMovementComponent::SetNetworkPolicyHash(uint64 Value)
{
    NetworkPolicyHash = Value;
}

uint64 PCharacterMovementComponent::GetNetworkPolicyHash() const
{
    return NetworkPolicyHash;
}

bool PCharacterMovementComponent::EnqueueServerMove(
    const FCharacterNetworkMove& Move)
{
    const bool bInvalidPayload = Move.Sequence == 0 || !std::isfinite(Move.DeltaSeconds)
        || Move.DeltaSeconds <= 0.0f || Move.DeltaSeconds > 0.125f
        || !std::isfinite(Move.Input.WorldInput.X)
        || !std::isfinite(Move.Input.WorldInput.Y)
        || !std::isfinite(Move.Input.WorldInput.Z)
        || Move.Input.WorldInput.SizeSquared() > 1.21f
        || !std::isfinite(Move.ControlYaw);
    if (bInvalidPayload || Move.PolicyHash != NetworkPolicyHash)
    {
        if (!bLoggedRejectedServerMove)
        {
            bLoggedRejectedServerMove = true;
            PICO_LOG(LogNet, Warning,
                "CharacterMove rejected for '{}': sequence={} invalidPayload={} clientPolicy={} serverPolicy={}",
                GetOwner() != nullptr ? GetOwner()->GetPathName() : std::string("<null>"),
                Move.Sequence, bInvalidPayload, Move.PolicyHash, NetworkPolicyHash);
        }
        return false;
    }
    if (Move.Sequence == LastProcessedNetworkMove
        || !IsNetSequenceNewer(Move.Sequence, LastProcessedNetworkMove))
        return true;
    if (std::any_of(ServerMoves.begin(), ServerMoves.end(),
            [&Move](const FCharacterNetworkMove& Existing)
            {
                return Existing.Sequence == Move.Sequence;
            }))
        return true;
    if (ServerMoves.size() >= MaxServerNetworkMoves)
    {
        if (!bLoggedRejectedServerMove)
        {
            bLoggedRejectedServerMove = true;
            PICO_LOG(LogNet, Warning,
                "CharacterMove rejected for '{}': server move queue is full",
                GetOwner() != nullptr ? GetOwner()->GetPathName() : std::string("<null>"));
        }
        return false;
    }
    const auto InsertAt = std::find_if(
        ServerMoves.begin(), ServerMoves.end(),
        [&Move](const FCharacterNetworkMove& Existing)
        {
            return IsNetSequenceNewer(Existing.Sequence, Move.Sequence);
        });
    ServerMoves.insert(InsertAt, Move);
    if (!bLoggedAcceptedServerMove)
    {
        bLoggedAcceptedServerMove = true;
        PICO_LOG(LogNet, Info,
            "CharacterMove accepted for '{}': first sequence={} policy={}",
            GetOwner() != nullptr ? GetOwner()->GetPathName() : std::string("<null>"),
            Move.Sequence, Move.PolicyHash);
    }
    return true;
}

void PCharacterMovementComponent::ReceiveNetworkCorrection(
    const FCharacterNetworkState& State)
{
    if (bHasPendingCorrection
        && !IsNetSequenceNewer(
            State.LastProcessedMove, PendingCorrection.LastProcessedMove))
        return;
    PendingCorrection = State;
    bHasPendingCorrection = true;
    PWorld* World = GetWorld();
    if (World != nullptr && State.LastProcessedMoveClientTimeSeconds > 0.0)
    {
        const float Sample = static_cast<float>(std::max(
            0.0,
            World->GetTimeSeconds()
                - State.LastProcessedMoveClientTimeSeconds));
        SmoothedMoveRoundTripSeconds = SmoothedMoveRoundTripSeconds <= 0.0f
            ? Sample
            : SmoothedMoveRoundTripSeconds * 0.9f + Sample * 0.1f;
    }
}

void PCharacterMovementComponent::ReceiveSimulatedSnapshot(
    const FCharacterNetworkState& State)
{
    if (State.ServerTick == 0
        || (LastReceivedServerTick != 0
            && !IsNetSequenceNewer(State.ServerTick, LastReceivedServerTick)))
        return;
    PWorld* World = GetWorld();
    const double LocalNow = World != nullptr ? World->GetTimeSeconds() : 0.0;
    if (LastSnapshotReceiveLocalTimeSeconds > 0.0 && LocalNow > 0.0)
        SnapshotReceiveIntervalSeconds = static_cast<float>(
            LocalNow - LastSnapshotReceiveLocalTimeSeconds);
    if (LastReceivedServerTimeSeconds > 0.0
        && State.ServerTimeSeconds > LastReceivedServerTimeSeconds)
    {
        SnapshotServerIntervalSeconds = static_cast<float>(
            State.ServerTimeSeconds - LastReceivedServerTimeSeconds);
        if (SnapshotReceiveIntervalSeconds > 0.0f)
        {
            const float JitterSample = std::abs(
                SnapshotReceiveIntervalSeconds - SnapshotServerIntervalSeconds);
            SmoothedSnapshotJitterSeconds = bHasAdaptiveSmoothingSample
                ? SmoothedSnapshotJitterSeconds * 0.9f + JitterSample * 0.1f
                : JitterSample;
            const float TargetSmoothTime = std::clamp(
                SnapshotServerIntervalSeconds * 1.25f
                    + SmoothedSnapshotJitterSeconds * 2.0f,
                NetworkMinAdaptiveSmoothTime,
                NetworkMaxAdaptiveSmoothTime);
            EffectiveNetworkSmoothingTimeSeconds = bHasAdaptiveSmoothingSample
                ? EffectiveNetworkSmoothingTimeSeconds * 0.75f
                    + TargetSmoothTime * 0.25f
                : TargetSmoothTime;
            bHasAdaptiveSmoothingSample = true;
        }
    }

    if (LocalNow > 0.0 && State.ServerTimeSeconds > 0.0)
    {
        double RoundTripSeconds = 0.0;
        if (World != nullptr && World->GetNetDriver() != nullptr)
        {
            const std::vector<FNetConnectionSnapshot> Connections =
                World->GetNetDriver()->GetConnectionSnapshots();
            if (!Connections.empty())
                RoundTripSeconds =
                    Connections.front().Statistics.SmoothedRoundTripSeconds;
        }
        const double OffsetSample = LocalNow - State.ServerTimeSeconds
            - std::max(0.0, RoundTripSeconds * 0.5);
        SmoothedServerClockOffsetSeconds = bHasServerClockOffsetSample
            ? SmoothedServerClockOffsetSeconds * 0.9 + OffsetSample * 0.1
            : OffsetSample;
        bHasServerClockOffsetSample = true;
        EstimatedSnapshotTransitSeconds = static_cast<float>(std::max(
            0.0,
            LocalNow - (State.ServerTimeSeconds
                + SmoothedServerClockOffsetSeconds)));
    }

    LastSnapshotReceiveLocalTimeSeconds = LocalNow;
    LastReceivedServerTimeSeconds = State.ServerTimeSeconds;
    LastReceivedServerTick = State.ServerTick;
    SimulatedProxySnapshotAgeSeconds = 0.0f;
    SimulatedProxyExtrapolationSeconds = 0.0f;
    bSimulatedProxyExtrapolationClamped = false;
    const ENetworkSmoothingMode Mode = GetNetworkSmoothingMode();
    if (ActiveNetworkSmoothingMode != Mode)
    {
        ClearNetworkMeshSmoothing();
        SimulatedSnapshots.clear();
        SimulatedPlaybackTick = 0.0f;
        ActiveNetworkSmoothingMode = Mode;
    }
    if (Mode == ENetworkSmoothingMode::Disabled)
    {
        ClearNetworkMeshSmoothing();
        SimulatedSnapshots.clear();
        ApplyMoveState(State.State);
        return;
    }
    if (Mode == ENetworkSmoothingMode::Linear
        || Mode == ENetworkSmoothingMode::Exponential)
    {
        SimulatedSnapshots.clear();
        ApplyNetworkSnapshotWithMeshSmoothing(State);
        return;
    }
    if (SimulatedSnapshots.size() >= MaxSimulatedSnapshots)
        SimulatedSnapshots.pop_front();
    SimulatedSnapshots.push_back(State);
    if (SimulatedSnapshots.size() == 1)
    {
        SimulatedPlaybackTick = static_cast<float>(State.ServerTick);
        ApplyMoveState(State.State);
    }
}

void PCharacterMovementComponent::RefreshNetworkSmoothingMeshes()
{
    NetworkSmoothingMeshes.clear();
    PCharacter* Character = GetCharacterOwner();
    if (Character == nullptr) return;
    for (PActorComponent* Component : Character->GetComponents())
    {
        if (Component == nullptr
            || !Component->IsA(PSkeletalMeshComponent::StaticClass()))
            continue;
        auto* Mesh = static_cast<PSkeletalMeshComponent*>(Component);
        FNetworkSmoothingMeshState MeshState;
        MeshState.ComponentHandle = Mesh->GetHandle();
        MeshState.StartVisualTransform = Mesh->GetVisualWorldTransform();
        MeshState.TargetVisualTransform = MeshState.StartVisualTransform;
        NetworkSmoothingMeshes.push_back(MeshState);
    }
}

void PCharacterMovementComponent::ClearNetworkMeshSmoothing()
{
    PCharacter* Character = GetCharacterOwner();
    if (Character != nullptr)
    {
        for (PActorComponent* Component : Character->GetComponents())
        {
            if (Component != nullptr
                && Component->IsA(PSkeletalMeshComponent::StaticClass()))
            {
                static_cast<PSkeletalMeshComponent*>(Component)
                    ->ClearNetworkSmoothingVisualTransform();
            }
        }
    }
    NetworkSmoothingMeshes.clear();
    NetworkSmoothingElapsedSeconds = 0.0f;
    NetworkSmoothingVisualOffsetDistance = 0.0f;
}

void PCharacterMovementComponent::ApplyNetworkSnapshotWithMeshSmoothing(
    const FCharacterNetworkState& State)
{
    const FCharacterMoveState PreviousState = CaptureMoveState();
    RefreshNetworkSmoothingMeshes();
    for (FNetworkSmoothingMeshState& MeshState : NetworkSmoothingMeshes)
    {
        PObject* Object = ResolveObject(MeshState.ComponentHandle);
        if (Object != nullptr
            && Object->IsA(PSkeletalMeshComponent::StaticClass()))
        {
            static_cast<PSkeletalMeshComponent*>(Object)
                ->ClearNetworkSmoothingVisualTransform();
        }
    }
    ApplyMoveState(State.State);

    const float CorrectionDistance = (
        State.State.Transform.Translation
        - PreviousState.Transform.Translation).Size();
    FinalizeNetworkMeshSmoothing(CorrectionDistance);
}

void PCharacterMovementComponent::FinalizeNetworkMeshSmoothing(
    float CorrectionDistance)
{
    if (CorrectionDistance > NetworkNoSmoothUpdateDistance)
    {
        NetworkSmoothingMeshes.clear();
        NetworkSmoothingVisualOffsetDistance = 0.0f;
        return;
    }

    NetworkSmoothingElapsedSeconds = 0.0f;
    NetworkSmoothingVisualOffsetDistance = 0.0f;
    for (FNetworkSmoothingMeshState& MeshState : NetworkSmoothingMeshes)
    {
        PObject* Object = ResolveObject(MeshState.ComponentHandle);
        auto* Mesh = Object != nullptr
                && Object->IsA(PSkeletalMeshComponent::StaticClass())
            ? static_cast<PSkeletalMeshComponent*>(Object) : nullptr;
        if (Mesh == nullptr) continue;
        MeshState.TargetVisualTransform = Mesh->GetVisualWorldTransform();
        FVector3 Offset = MeshState.StartVisualTransform.Translation
            - MeshState.TargetVisualTransform.Translation;
        const float OffsetDistance = Offset.Size();
        if (OffsetDistance > NetworkMaxSmoothUpdateDistance
            && OffsetDistance > SmallNumber)
        {
            MeshState.StartVisualTransform.Translation =
                MeshState.TargetVisualTransform.Translation
                + Offset * (NetworkMaxSmoothUpdateDistance / OffsetDistance);
        }
        NetworkSmoothingVisualOffsetDistance = std::max(
            NetworkSmoothingVisualOffsetDistance,
            (MeshState.StartVisualTransform.Translation
                - MeshState.TargetVisualTransform.Translation).Size());
        Mesh->SetNetworkSmoothingVisualTransform(
            MeshState.StartVisualTransform);
    }
}

void PCharacterMovementComponent::TickNetworkMeshSmoothing(float DeltaSeconds)
{
    const ENetworkSmoothingMode Mode = GetNetworkSmoothingMode();
    if ((Mode != ENetworkSmoothingMode::Linear
            && Mode != ENetworkSmoothingMode::Exponential)
        || NetworkSmoothingMeshes.empty())
        return;
    const float SmoothTime = std::max(
        GetEffectiveNetworkSmoothingTime(), 0.001f);
    const float Step = std::max(DeltaSeconds, 0.0f);
    NetworkSmoothingElapsedSeconds += Step;
    const float LinearAlpha = std::clamp(
        NetworkSmoothingElapsedSeconds / SmoothTime, 0.0f, 1.0f);
    const float ExponentialAlpha = std::clamp(Step / SmoothTime, 0.0f, 1.0f);
    bool bAllComplete = true;
    NetworkSmoothingVisualOffsetDistance = 0.0f;
    for (FNetworkSmoothingMeshState& MeshState : NetworkSmoothingMeshes)
    {
        PObject* Object = ResolveObject(MeshState.ComponentHandle);
        auto* Mesh = Object != nullptr
                && Object->IsA(PSkeletalMeshComponent::StaticClass())
            ? static_cast<PSkeletalMeshComponent*>(Object) : nullptr;
        if (Mesh == nullptr) continue;
        const FTransform Current = Mesh->GetVisualWorldTransform();
        const FTransform Smoothed = InterpolateTransform(
            Mode == ENetworkSmoothingMode::Linear
                ? MeshState.StartVisualTransform : Current,
            MeshState.TargetVisualTransform,
            Mode == ENetworkSmoothingMode::Linear
                ? LinearAlpha : ExponentialAlpha);
        Mesh->SetNetworkSmoothingVisualTransform(Smoothed);
        const float Remaining = (
            Smoothed.Translation
            - MeshState.TargetVisualTransform.Translation).Size();
        NetworkSmoothingVisualOffsetDistance = std::max(
            NetworkSmoothingVisualOffsetDistance, Remaining);
        bAllComplete = bAllComplete
            && Remaining <= 0.01f
            && Smoothed.Rotation.Rotator().Equals(
                MeshState.TargetVisualTransform.Rotation.Rotator(), 0.01f)
            && Smoothed.Scale.Equals(
                MeshState.TargetVisualTransform.Scale, 0.001f);
    }
    if (Mode == ENetworkSmoothingMode::Linear && LinearAlpha >= 1.0f)
        bAllComplete = true;
    if (Mode == ENetworkSmoothingMode::Exponential
        && NetworkSmoothingElapsedSeconds >= SmoothTime * 6.0f)
        bAllComplete = true;
    if (bAllComplete) ClearNetworkMeshSmoothing();
}

void PCharacterMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
    TickNetworkMeshSmoothing(DeltaSeconds);
}

void PCharacterMovementComponent::TranslateNetworkSmoothingTargets(
    const FVector3& TranslationDelta)
{
    if (TranslationDelta.IsNearlyZero()) return;
    for (FNetworkSmoothingMeshState& MeshState : NetworkSmoothingMeshes)
    {
        PObject* Object = ResolveObject(MeshState.ComponentHandle);
        auto* Mesh = Object != nullptr
                && Object->IsA(PSkeletalMeshComponent::StaticClass())
            ? static_cast<PSkeletalMeshComponent*>(Object) : nullptr;
        if (Mesh != nullptr && Mesh->HasNetworkSmoothingVisualTransform())
        {
            FTransform VisualTransform = Mesh->GetVisualWorldTransform();
            VisualTransform.Translation += TranslationDelta;
            Mesh->SetNetworkSmoothingVisualTransform(VisualTransform);
        }
        MeshState.StartVisualTransform.Translation += TranslationDelta;
        MeshState.TargetVisualTransform.Translation += TranslationDelta;
    }
}

uint32 PCharacterMovementComponent::GetLastProcessedNetworkMove() const
{
    return LastProcessedNetworkMove;
}
double PCharacterMovementComponent::GetLastProcessedMoveClientTimeSeconds() const
{
    return LastProcessedMoveClientTimeSeconds;
}

FCharacterPredictionStatistics
PCharacterMovementComponent::GetPredictionStatistics() const
{
    FCharacterPredictionStatistics Result = PredictionStatistics;
    Result.PendingMoveCount = PendingMoves.size();
    Result.SnapshotCount = SimulatedSnapshots.size();
    Result.ServerMoveQueueCount = ServerMoves.size();
    Result.SimulatedProxySnapshotAgeSeconds =
        SimulatedProxySnapshotAgeSeconds;
    Result.SimulatedProxyExtrapolationSeconds =
        SimulatedProxyExtrapolationSeconds;
    Result.SnapshotReceiveIntervalSeconds = SnapshotReceiveIntervalSeconds;
    Result.SnapshotServerIntervalSeconds = SnapshotServerIntervalSeconds;
    Result.SmoothedSnapshotJitterSeconds = SmoothedSnapshotJitterSeconds;
    Result.EstimatedSnapshotTransitSeconds = EstimatedSnapshotTransitSeconds;
    Result.SmoothedServerClockOffsetSeconds = static_cast<float>(
        SmoothedServerClockOffsetSeconds);
    Result.EffectiveNetworkSmoothingTimeSeconds =
        GetEffectiveNetworkSmoothingTime();
    Result.SmoothedMoveRoundTripSeconds = SmoothedMoveRoundTripSeconds;
    Result.bPredictionEnabled = bPredictionEnabled;
    Result.bPolicyHashMatches = bPolicyHashMatches;
    return Result;
}

void PCharacterMovementComponent::SimulateProxyMovement(float DeltaSeconds)
{
    PSceneComponent* Updated = GetUpdatedComponent();
    if (!bEnableSimulatedProxyExtrapolation
        || Updated == nullptr
        || LastReceivedServerTick == 0
        || !std::isfinite(DeltaSeconds)
        || DeltaSeconds <= 0.0f
        || GetMovementMode() == EMovementMode::None)
        return;

    const float Remaining = NetworkMaxSimulatedProxyExtrapolationTime
        - SimulatedProxyExtrapolationSeconds;
    if (Remaining <= SmallNumber)
    {
        if (!bSimulatedProxyExtrapolationClamped)
        {
            bSimulatedProxyExtrapolationClamped = true;
            ++PredictionStatistics.SimulatedProxyExtrapolationClampCount;
        }
        return;
    }

    const float Step = std::min(DeltaSeconds, Remaining);
    const FVector3 PreviousLocation = Updated->GetWorldTransform().Translation;
    FVector3 NewVelocity = GetVelocity();
    if (GetMovementMode() == EMovementMode::Walking)
        NewVelocity.Z = 0.0f;
    else if (GetMovementMode() == EMovementMode::Falling)
        NewVelocity.Z += GravityZ * GravityScale * Step;

    const FVector3 Delta = NewVelocity * Step;
    FHitResult Hit;
    SafeMoveUpdatedComponent(
        Delta, Updated->GetWorldTransform().Rotation, true, &Hit);
    if (Hit.bBlockingHit)
    {
        if (NewVelocity.Z <= 0.0f && IsWalkable(Hit))
        {
            NewVelocity.Z = 0.0f;
            SetMovementMode(EMovementMode::Walking);
            FindFloor(CurrentFloor);
        }
        else
        {
            const FVector3 Normal = Hit.Normal.GetSafeNormal();
            SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
            NewVelocity -= Normal * FVector3::Dot(NewVelocity, Normal);
        }
    }
    SetVelocity(NewVelocity);
    SimulatedProxyExtrapolationSeconds += Step;
    if (SimulatedProxyExtrapolationSeconds
            >= NetworkMaxSimulatedProxyExtrapolationTime - SmallNumber
        && !bSimulatedProxyExtrapolationClamped)
    {
        bSimulatedProxyExtrapolationClamped = true;
        ++PredictionStatistics.SimulatedProxyExtrapolationClampCount;
    }
    TranslateNetworkSmoothingTargets(
        Updated->GetWorldTransform().Translation - PreviousLocation);
}

void PCharacterMovementComponent::TickAuthorityNetworkMovement(float)
{
    PCharacter* Character = GetCharacterOwner();
    if (!bLoggedAuthorityStart)
    {
        bLoggedAuthorityStart = true;
        PICO_LOG(LogNet, Info,
            "CharacterMovement authority tick started for '{}' policy={} ownerConnectionValid={}",
            Character != nullptr ? Character->GetPathName() : std::string("<null>"),
            NetworkPolicyHash,
            Character != nullptr && Character->GetWorld() != nullptr
                && Character->GetWorld()->GetNetDriver() != nullptr
                && Character->GetWorld()->GetNetDriver()
                    ->GetActorOwningConnection(Character).IsValid());
    }
    std::size_t Processed = 0;
    while (!ServerMoves.empty() && Processed < MaxMovesPerServerTick)
    {
        FCharacterNetworkMove Move = ServerMoves.front();
        ServerMoves.pop_front();
        if (PController* Controller = Character->GetController())
        {
            FRotator Rotation = Controller->GetControlRotation();
            Rotation.Yaw = FRotator::NormalizeAxis(Move.ControlYaw);
            Controller->SetControlRotation(Rotation);
        }
        SimulateMovement(Move.Input, Move.DeltaSeconds);
        LastProcessedNetworkMove = Move.Sequence;
        LastProcessedMoveClientTimeSeconds = Move.ClientTimeSeconds;
        ++Processed;
    }
}

void PCharacterMovementComponent::ApplyPendingCorrection()
{
    if (!bHasPendingCorrection) return;
    bHasPendingCorrection = false;
    bPolicyHashMatches = PendingCorrection.PolicyHash == NetworkPolicyHash;
    PredictionStatistics.LastAcknowledgedMove =
        PendingCorrection.LastProcessedMove;
    FCharacterMoveState ComparedState = CaptureMoveState();
    if (PendingCorrection.LastProcessedMove == 0 && !PendingMoves.empty())
    {
        ComparedState = PendingMoves.front().StartState;
    }
    else
    {
        const auto Acknowledged = std::find_if(
            PendingMoves.begin(), PendingMoves.end(),
            [this](const FCharacterNetworkMove& Move)
            {
                return Move.Sequence == PendingCorrection.LastProcessedMove;
            });
        if (Acknowledged != PendingMoves.end())
            ComparedState = Acknowledged->PredictedState;
    }
    while (!PendingMoves.empty()
        && IsAcknowledged(
            PendingMoves.front().Sequence,
            PendingCorrection.LastProcessedMove))
    {
        PendingMoves.pop_front();
    }

    const float PositionError = (
        ComparedState.Transform.Translation
        - PendingCorrection.State.Transform.Translation).Size();
    const float VelocityError = (
        ComparedState.Velocity - PendingCorrection.State.Velocity).Size();
    const bool bNeedsCorrection = !bPolicyHashMatches
        || PositionError > NetworkCorrectionDistance
        || VelocityError > NetworkCorrectionVelocity
        || ComparedState.MovementMode != PendingCorrection.State.MovementMode;
    bPredictionEnabled = bPolicyHashMatches;
    if (!bNeedsCorrection) return;

    const FVector3 PreviousLocation = CaptureMoveState().Transform.Translation;
    const ENetworkSmoothingMode SmoothingMode = GetNetworkSmoothingMode();
    const bool bSmoothVisualCorrection =
        SmoothingMode == ENetworkSmoothingMode::Linear
        || SmoothingMode == ENetworkSmoothingMode::Exponential;
    if (bSmoothVisualCorrection)
    {
        RefreshNetworkSmoothingMeshes();
        for (FNetworkSmoothingMeshState& MeshState : NetworkSmoothingMeshes)
        {
            PObject* Object = ResolveObject(MeshState.ComponentHandle);
            if (Object != nullptr
                && Object->IsA(PSkeletalMeshComponent::StaticClass()))
            {
                static_cast<PSkeletalMeshComponent*>(Object)
                    ->ClearNetworkSmoothingVisualTransform();
            }
        }
    }
    ApplyMoveState(PendingCorrection.State);
    ++PredictionStatistics.CorrectionCount;
    PredictionStatistics.MaxPositionError = std::max(
        PredictionStatistics.MaxPositionError, PositionError);
    if (!bPredictionEnabled)
    {
        PendingMoves.clear();
        if (bSmoothVisualCorrection)
        {
            FinalizeNetworkMeshSmoothing((
                CaptureMoveState().Transform.Translation
                - PreviousLocation).Size());
        }
        return;
    }
    PCharacter* Character = GetCharacterOwner();
    for (FCharacterNetworkMove& Move : PendingMoves)
    {
        if (PController* Controller = Character->GetController())
        {
            FRotator Rotation = Controller->GetControlRotation();
            Rotation.Yaw = FRotator::NormalizeAxis(Move.ControlYaw);
            Controller->SetControlRotation(Rotation);
        }
        Move.StartState = CaptureMoveState();
        SimulateMovement(Move.Input, Move.DeltaSeconds);
        Move.PredictedState = CaptureMoveState();
        ++PredictionStatistics.ReplayCount;
    }
    if (bSmoothVisualCorrection)
    {
        FinalizeNetworkMeshSmoothing((
            CaptureMoveState().Transform.Translation
            - PreviousLocation).Size());
    }
}

void PCharacterMovementComponent::TickAutonomousNetworkMovement(
    const FCharacterMoveInput& Input, float DeltaSeconds)
{
    ApplyPendingCorrection();
    PCharacter* Character = GetCharacterOwner();
    if (!bLoggedAutonomousStart)
    {
        bLoggedAutonomousStart = true;
        PICO_LOG(LogNet, Info,
            "CharacterMovement autonomous tick started for '{}' policy={} controller={}",
            Character != nullptr ? Character->GetPathName() : std::string("<null>"),
            NetworkPolicyHash,
            Character != nullptr && Character->GetController() != nullptr
                ? Character->GetController()->GetPathName() : std::string("<null>"));
    }
    FCharacterNetworkMove Move;
    Move.Sequence = NextMoveSequence++;
    if (NextMoveSequence == 0) NextMoveSequence = 1;
    Move.DeltaSeconds = std::clamp(DeltaSeconds, 0.001f, 0.125f);
    Move.ClientTimeSeconds = Character->GetWorld() != nullptr
        ? Character->GetWorld()->GetTimeSeconds() : 0.0;
    Move.Input = Input;
    Move.Input.RootMotionDelta = FTransform::Identity;
    Move.ControlYaw = Character->GetController() != nullptr
        ? Character->GetController()->GetControlRotation().Yaw : 0.0f;
    Move.PolicyHash = NetworkPolicyHash;
    if (bPredictionEnabled)
    {
        Move.StartState = CaptureMoveState();
        SimulateMovement(Move.Input, Move.DeltaSeconds);
        Move.PredictedState = CaptureMoveState();
        if (PendingMoves.size() >= MaxPendingNetworkMoves)
        {
            PendingMoves.pop_front();
            ++PredictionStatistics.DroppedMoveCount;
        }
        PendingMoves.push_back(Move);
    }

    FNetDriver* Driver = Character->GetWorld() != nullptr
        ? Character->GetWorld()->GetNetDriver() : nullptr;
    if (Driver == nullptr) return;
    std::array<FCharacterNetworkMove, MovesPerPacket> PacketMoves;
    const std::size_t Count = std::min(PendingMoves.size(), MovesPerPacket);
    for (std::size_t Index = 0; Index < Count; ++Index)
        PacketMoves[Index] = PendingMoves[PendingMoves.size() - Count + Index];
    if (Count == 0)
    {
        PacketMoves[0] = Move;
    }
    const std::size_t SendCount = Count == 0 ? 1 : Count;
    if (Driver->QueueCharacterMoves(
            Character,
            std::span<const FCharacterNetworkMove>(
                PacketMoves.data(), SendCount)))
        PredictionStatistics.LastSentMove = Move.Sequence;
    else if (!bLoggedMoveSendFailure)
    {
        bLoggedMoveSendFailure = true;
        PICO_LOG(LogNet, Warning,
            "CharacterMove could not be queued for '{}' sequence={} role={} policy={}",
            Character->GetPathName(), Move.Sequence,
            static_cast<int>(Character->GetLocalRole()), NetworkPolicyHash);
    }
}

void PCharacterMovementComponent::TickSimulatedNetworkMovement(
    float DeltaSeconds)
{
    SimulatedProxySnapshotAgeSeconds += std::max(DeltaSeconds, 0.0f);
    if (GetNetworkSmoothingMode()
        != ENetworkSmoothingMode::SnapshotInterpolation)
    {
        SimulateProxyMovement(DeltaSeconds);
        return;
    }
    if (SimulatedSnapshots.empty()) return;
    const float LatestTick = static_cast<float>(
        SimulatedSnapshots.back().ServerTick);
    const float TargetTick = std::max(
        static_cast<float>(SimulatedSnapshots.front().ServerTick),
        LatestTick - SnapshotInterpolationDelayTicks);
    SimulatedPlaybackTick = std::min(
        TargetTick,
        SimulatedPlaybackTick + std::max(DeltaSeconds, 0.0f)
            * NetworkSimulationRate);
    while (SimulatedSnapshots.size() > 2
        && static_cast<float>(SimulatedSnapshots[1].ServerTick)
            <= SimulatedPlaybackTick)
        SimulatedSnapshots.pop_front();
    if (SimulatedSnapshots.size() == 1)
    {
        ApplyMoveState(SimulatedSnapshots.front().State);
        return;
    }
    const FCharacterNetworkState& A = SimulatedSnapshots[0];
    const FCharacterNetworkState& B = SimulatedSnapshots[1];
    const float Span = std::max(
        1.0f, static_cast<float>(B.ServerTick - A.ServerTick));
    const float Alpha = std::clamp(
        (SimulatedPlaybackTick - static_cast<float>(A.ServerTick)) / Span,
        0.0f, 1.0f);
    ApplyMoveState(InterpolateState(A.State, B.State, Alpha));
}

void PCharacterMovementComponent::TickComponent(float DeltaSeconds)
{
    PPawnMovementComponent::TickComponent(DeltaSeconds);
    PCharacter* Character = GetCharacterOwner();
    if (Character == nullptr) return;
    FCharacterMoveInput Input;
    Input.WorldInput = ConsumeInputVector();
    Input.bJumpPressed = Character->ConsumeJumpInput();
    Input.RootMotionDelta = PendingRootMotion;
    PendingRootMotion = FTransform::Identity;
    PWorld* World = Character->GetWorld();
    FNetDriver* Driver = World != nullptr ? World->GetNetDriver() : nullptr;
    if (Driver == nullptr || Driver->GetNetMode() == ENetMode::Standalone)
    {
        TickNetworkMeshSmoothing(DeltaSeconds);
        SimulateMovement(Input, DeltaSeconds);
        return;
    }
    if (Character->GetLocalRole() == ENetRole::SimulatedProxy)
    {
        TickSimulatedNetworkMovement(DeltaSeconds);
        TickNetworkMeshSmoothing(DeltaSeconds);
        return;
    }
    TickNetworkMeshSmoothing(DeltaSeconds);
    if (Character->GetLocalRole() == ENetRole::AutonomousProxy)
    {
        TickAutonomousNetworkMovement(Input, DeltaSeconds);
        return;
    }
    if (Character->GetLocalRole() == ENetRole::Authority
        && Driver->GetActorOwningConnection(Character).IsValid())
    {
        TickAuthorityNetworkMovement(DeltaSeconds);
        return;
    }
    SimulateMovement(Input, DeltaSeconds);
}
}
