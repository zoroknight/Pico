#pragma once

#include "Pico/Engine/PawnMovementComponent.h"

namespace Pico
{
class PCharacter;
class PPrimitiveComponent;

enum class EMovementMode : uint8
{
    None,
    Walking,
    Falling
};

const char* ToString(EMovementMode Mode);

struct FCharacterMoveInput
{
    FVector3 WorldInput = FVector3::ZeroVector;
    bool bJumpPressed = false;
    FTransform RootMotionDelta;
};

struct FCharacterMoveState
{
    FTransform Transform;
    FVector3 Velocity = FVector3::ZeroVector;
    EMovementMode MovementMode = EMovementMode::Falling;
};

struct FFindFloorResult
{
    FHitResult HitResult;
    float FloorDistance = 0.0f;
    bool bBlockingHit = false;
    bool bWalkableFloor = false;
};

class PCharacterMovementComponent : public PPawnMovementComponent
{
    PICO_DECLARE_CLASS(PCharacterMovementComponent, PPawnMovementComponent)

public:
    PCharacter* GetCharacterOwner() const;
    EMovementMode GetMovementMode() const;
    bool IsMovingOnGround() const;
    void SetMovementMode(EMovementMode Mode);
    const FFindFloorResult& GetCurrentFloor() const;
    int32 GetLastSimulationIterations() const;
    const FCharacterMoveInput& GetLastSimulationInput() const;

    float GetMaxWalkSpeed() const;
    void SetMaxWalkSpeed(float Value);
    float GetGroundAcceleration() const;
    void SetGroundAcceleration(float Value);
    float GetBrakingDeceleration() const;
    void SetBrakingDeceleration(float Value);
    float GetAirControl() const;
    void SetAirControl(float Value);
    float GetGravityScale() const;
    void SetGravityScale(float Value);
    float GetJumpZVelocity() const;
    void SetJumpZVelocity(float Value);
    float GetWalkableFloorAngle() const;
    void SetWalkableFloorAngle(float Value);
    float GetMaxSimulationDeltaTime() const;
    void SetMaxSimulationDeltaTime(float Value);
    int32 GetMaxSimulationIterations() const;
    void SetMaxSimulationIterations(int32 Value);
    float GetPushImpulse() const;
    void SetPushImpulse(float Value);
    bool ShouldOrientRotationToMovement() const;
    void SetOrientRotationToMovement(bool bValue);
    float GetRotationRate() const;
    void SetRotationRate(float Value);

    FCharacterMoveState CaptureMoveState() const;
    bool ApplyMoveState(const FCharacterMoveState& State);
    bool FindFloor(FFindFloorResult& OutFloor) const;
    bool IsWalkable(const FHitResult& Hit) const;
    void SimulateMovement(const FCharacterMoveInput& Input, float DeltaSeconds);
    void QueueRootMotion(const FTransform& Delta);
    void TickComponent(float DeltaSeconds) override;

protected:
    explicit PCharacterMovementComponent(const FObjectConstructionParams& Params);

private:
    void SimulateStep(const FCharacterMoveInput& Input, float DeltaSeconds);
    void SimulateWalking(const FCharacterMoveInput& Input, float DeltaSeconds);
    void SimulateFalling(const FCharacterMoveInput& Input, float DeltaSeconds);
    void HandleImpact(const FHitResult& Hit, const FVector3& MoveDelta);

    float MaxWalkSpeed = 250.0f;
    float GroundAcceleration = 1200.0f;
    float BrakingDeceleration = 1600.0f;
    float AirControl = 0.35f;
    float GravityScale = 1.0f;
    float JumpZVelocity = 420.0f;
    float WalkableFloorAngle = 45.0f;
    float FloorProbeDistance = 8.0f;
    float MaxSimulationDeltaTime = 1.0f / 30.0f;
    int32 MaxSimulationIterations = 4;
    float PushImpulse = 250.0f;
    bool bOrientRotationToMovement = true;
    float RotationRate = 540.0f;
    int32 MovementModeValue = static_cast<int32>(EMovementMode::Falling);
    int32 LastSimulationIterations = 0;
    FFindFloorResult CurrentFloor;
    FCharacterMoveInput LastSimulationInput;
    FTransform PendingRootMotion;
};
}
