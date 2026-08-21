#pragma once

#include "Pico/Engine/PawnMovementComponent.h"

#include <deque>
#include <vector>

namespace Pico
{
class PCharacter;
class PPrimitiveComponent;
class PSkeletalMeshComponent;

enum class EMovementMode : uint8
{
    None,
    Walking,
    Falling
};

const char* ToString(EMovementMode Mode);

enum class ENetworkSmoothingMode : uint8
{
    Disabled,
    Linear,
    Exponential,
    SnapshotInterpolation
};

const char* ToString(ENetworkSmoothingMode Mode);

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

struct FCharacterNetworkMove
{
    uint32 Sequence = 0;
    double ClientTimeSeconds = 0.0;
    float DeltaSeconds = 0.0f;
    FCharacterMoveInput Input;
    float ControlYaw = 0.0f;
    FCharacterMoveState StartState;
    FCharacterMoveState PredictedState;
    uint64 PolicyHash = 0;
};

struct FCharacterNetworkState
{
    uint32 ServerTick = 0;
    double ServerTimeSeconds = 0.0;
    uint32 LastProcessedMove = 0;
    double LastProcessedMoveClientTimeSeconds = 0.0;
    FCharacterMoveState State;
    uint64 PolicyHash = 0;
};

struct FCharacterPredictionStatistics
{
    uint32 LastSentMove = 0;
    uint32 LastAcknowledgedMove = 0;
    uint64 CorrectionCount = 0;
    uint64 ReplayCount = 0;
    uint64 DroppedMoveCount = 0;
    float MaxPositionError = 0.0f;
    std::size_t PendingMoveCount = 0;
    std::size_t SnapshotCount = 0;
    std::size_t ServerMoveQueueCount = 0;
    float SimulatedProxySnapshotAgeSeconds = 0.0f;
    float SimulatedProxyExtrapolationSeconds = 0.0f;
    uint64 SimulatedProxyExtrapolationClampCount = 0;
    float SnapshotReceiveIntervalSeconds = 0.0f;
    float SnapshotServerIntervalSeconds = 0.0f;
    float SmoothedSnapshotJitterSeconds = 0.0f;
    float EstimatedSnapshotTransitSeconds = 0.0f;
    float SmoothedServerClockOffsetSeconds = 0.0f;
    float EffectiveNetworkSmoothingTimeSeconds = 0.1f;
    float SmoothedMoveRoundTripSeconds = 0.0f;
    bool bPredictionEnabled = true;
    bool bPolicyHashMatches = true;
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
    bool UsesControllerDesiredRotation() const;
    void SetUseControllerDesiredRotation(bool bValue);
    float GetRotationRate() const;
    void SetRotationRate(float Value);
    ENetworkSmoothingMode GetNetworkSmoothingMode() const;
    void SetNetworkSmoothingMode(ENetworkSmoothingMode Mode);
    float GetNetworkSimulatedSmoothLocationTime() const;
    void SetNetworkSimulatedSmoothLocationTime(float Value);
    bool UsesAdaptiveNetworkSmoothing() const;
    void SetUseAdaptiveNetworkSmoothing(bool bValue);
    float GetNetworkMinAdaptiveSmoothTime() const;
    void SetNetworkMinAdaptiveSmoothTime(float Value);
    float GetNetworkMaxAdaptiveSmoothTime() const;
    void SetNetworkMaxAdaptiveSmoothTime(float Value);
    float GetEffectiveNetworkSmoothingTime() const;
    float GetNetworkMaxSmoothUpdateDistance() const;
    void SetNetworkMaxSmoothUpdateDistance(float Value);
    float GetNetworkNoSmoothUpdateDistance() const;
    void SetNetworkNoSmoothUpdateDistance(float Value);
    float GetSnapshotInterpolationDelayTicks() const;
    void SetSnapshotInterpolationDelayTicks(float Value);
    bool IsSimulatedProxyExtrapolationEnabled() const;
    void SetSimulatedProxyExtrapolationEnabled(bool bValue);
    float GetNetworkMaxSimulatedProxyExtrapolationTime() const;
    void SetNetworkMaxSimulatedProxyExtrapolationTime(float Value);
    float GetNetworkSmoothingVisualOffsetDistance() const;

    FCharacterMoveState CaptureMoveState() const;
    bool ApplyMoveState(const FCharacterMoveState& State);
    bool FindFloor(FFindFloorResult& OutFloor) const;
    bool IsWalkable(const FHitResult& Hit) const;
    void SimulateMovement(const FCharacterMoveInput& Input, float DeltaSeconds);
    void QueueRootMotion(const FTransform& Delta);
    void SetNetworkPolicyHash(uint64 Value);
    uint64 GetNetworkPolicyHash() const;
    bool EnqueueServerMove(const FCharacterNetworkMove& Move);
    void ReceiveNetworkCorrection(const FCharacterNetworkState& State);
    void ReceiveSimulatedSnapshot(const FCharacterNetworkState& State);
    void SimulateProxyMovement(float DeltaSeconds);
    void SmoothClientPosition(float DeltaSeconds);
    uint32 GetLastProcessedNetworkMove() const;
    double GetLastProcessedMoveClientTimeSeconds() const;
    FCharacterPredictionStatistics GetPredictionStatistics() const;
    void TickComponent(float DeltaSeconds) override;

protected:
    explicit PCharacterMovementComponent(const FObjectConstructionParams& Params);

private:
    void SimulateStep(const FCharacterMoveInput& Input, float DeltaSeconds);
    void SimulateWalking(const FCharacterMoveInput& Input, float DeltaSeconds);
    void SimulateFalling(const FCharacterMoveInput& Input, float DeltaSeconds);
    void HandleImpact(const FHitResult& Hit, const FVector3& MoveDelta);
    void TickAuthorityNetworkMovement(float DeltaSeconds);
    void TickAutonomousNetworkMovement(
        const FCharacterMoveInput& Input, float DeltaSeconds);
    void TickSimulatedNetworkMovement(float DeltaSeconds);
    void TickNetworkMeshSmoothing(float DeltaSeconds);
    void TranslateNetworkSmoothingTargets(const FVector3& TranslationDelta);
    void ApplyNetworkSnapshotWithMeshSmoothing(
        const FCharacterNetworkState& State);
    void FinalizeNetworkMeshSmoothing(float CorrectionDistance);
    void ClearNetworkMeshSmoothing();
    void RefreshNetworkSmoothingMeshes();
    void ApplyPendingCorrection();

    struct FNetworkSmoothingMeshState
    {
        FObjectHandle ComponentHandle;
        FTransform StartVisualTransform;
        FTransform TargetVisualTransform;
    };

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
    bool bUseControllerDesiredRotation = false;
    float RotationRate = 540.0f;
    float NetworkSimulatedSmoothLocationTime = 0.1f;
    bool bUseAdaptiveNetworkSmoothing = true;
    float NetworkMinAdaptiveSmoothTime = 0.025f;
    float NetworkMaxAdaptiveSmoothTime = 0.1f;
    float NetworkMaxSmoothUpdateDistance = 256.0f;
    float NetworkNoSmoothUpdateDistance = 384.0f;
    float SnapshotInterpolationDelayTicks = 2.0f;
    bool bEnableSimulatedProxyExtrapolation = true;
    float NetworkMaxSimulatedProxyExtrapolationTime = 0.2f;
    int32 MovementModeValue = static_cast<int32>(EMovementMode::Falling);
    int32 NetworkSmoothingModeValue =
        static_cast<int32>(ENetworkSmoothingMode::Exponential);
    int32 LastSimulationIterations = 0;
    FFindFloorResult CurrentFloor;
    FCharacterMoveInput LastSimulationInput;
    FTransform PendingRootMotion;
    std::deque<FCharacterNetworkMove> ServerMoves;
    std::deque<FCharacterNetworkMove> PendingMoves;
    std::deque<FCharacterNetworkState> SimulatedSnapshots;
    FCharacterNetworkState PendingCorrection;
    uint64 NetworkPolicyHash = 0;
    uint32 NextMoveSequence = 1;
    uint32 LastProcessedNetworkMove = 0;
    double LastProcessedMoveClientTimeSeconds = 0.0;
    uint32 LastReceivedServerTick = 0;
    double LastReceivedServerTimeSeconds = 0.0;
    double LastSnapshotReceiveLocalTimeSeconds = 0.0;
    double SmoothedServerClockOffsetSeconds = 0.0;
    float SnapshotReceiveIntervalSeconds = 0.0f;
    float SnapshotServerIntervalSeconds = 0.0f;
    float SmoothedSnapshotJitterSeconds = 0.0f;
    float EstimatedSnapshotTransitSeconds = 0.0f;
    float EffectiveNetworkSmoothingTimeSeconds = 0.1f;
    float SmoothedMoveRoundTripSeconds = 0.0f;
    float SimulatedPlaybackTick = 0.0f;
    float SimulatedProxySnapshotAgeSeconds = 0.0f;
    float SimulatedProxyExtrapolationSeconds = 0.0f;
    float NetworkSmoothingElapsedSeconds = 0.0f;
    float NetworkSmoothingVisualOffsetDistance = 0.0f;
    ENetworkSmoothingMode ActiveNetworkSmoothingMode =
        ENetworkSmoothingMode::Exponential;
    std::vector<FNetworkSmoothingMeshState> NetworkSmoothingMeshes;
    bool bHasPendingCorrection = false;
    bool bHasServerClockOffsetSample = false;
    bool bHasAdaptiveSmoothingSample = false;
    bool bSimulatedProxyExtrapolationClamped = false;
    bool bPredictionEnabled = true;
    bool bPolicyHashMatches = true;
    bool bLoggedAutonomousStart = false;
    bool bLoggedAuthorityStart = false;
    bool bLoggedAcceptedServerMove = false;
    bool bLoggedRejectedServerMove = false;
    bool bLoggedMoveSendFailure = false;
    FCharacterPredictionStatistics PredictionStatistics;
};
}
