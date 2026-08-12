#include "Pico/Engine/CharacterMovementComponent.h"

#include "Pico/Engine/Character.h"
#include "Pico/Engine/Controller.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
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
    PICO_ADD_PROPERTY(Properties, bOrientRotationToMovement);
    PICO_ADD_PROPERTY(Properties, RotationRate);
    FPropertyMetadata RuntimeMetadata;
    RuntimeMetadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
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
bool PCharacterMovementComponent::ShouldOrientRotationToMovement() const
{ return bOrientRotationToMovement; }
void PCharacterMovementComponent::SetOrientRotationToMovement(bool bValue)
{ bOrientRotationToMovement = bValue; }
float PCharacterMovementComponent::GetRotationRate() const { return RotationRate; }
void PCharacterMovementComponent::SetRotationRate(float Value)
{ if (std::isfinite(Value) && Value >= 0.0f) RotationRate = Value; }

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
    if (Component == nullptr || Query == nullptr) return false;

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
    if (!Hit.bBlockingHit || PushImpulse <= 0.0f) return;
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
    FVector3 Facing = Input.WorldInput;
    Facing.Z = 0.0f;
    if (Character != nullptr && !Facing.IsNearlyZero())
    {
        float TargetYaw = std::atan2(Facing.Y, Facing.X) * 180.0f / Pico::Pi;
        FRotator Rotation = Character->GetActorRotation();
        if (Character->UsesControllerRotationYaw() && Character->GetController() != nullptr)
            TargetYaw = Character->GetController()->GetControlRotation().Yaw;
        if (bOrientRotationToMovement || Character->UsesControllerRotationYaw())
        {
            const float DeltaYaw = FRotator::NormalizeAxis(TargetYaw - Rotation.Yaw);
            Rotation.Yaw += std::clamp(
                DeltaYaw, -RotationRate * DeltaSeconds, RotationRate * DeltaSeconds);
            Character->SetActorRotation(Rotation);
        }
    }
}

void PCharacterMovementComponent::QueueRootMotion(const FTransform& Delta)
{
    PendingRootMotion = PendingRootMotion * Delta;
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
    SimulateMovement(Input, DeltaSeconds);
}
}
