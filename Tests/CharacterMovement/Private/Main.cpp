#include "TestRunner.h"

#include "Pico/Engine/Character.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/Controller.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/PhysicsCore/PhysicsScene.h"

#include <cmath>
#include <deque>

namespace
{
Pico::PCubeComponent* SpawnCube(
    Pico::PWorld& World,
    const char* Name,
    const Pico::FVector3& Location,
    const Pico::FVector3& Extent,
    Pico::EPhysicsBodyType BodyType)
{
    Pico::PActor* Actor = World.SpawnActor<Pico::PActor>(Name);
    Pico::PCubeComponent* Cube = Actor != nullptr
        ? Actor->CreateComponent<Pico::PCubeComponent>("Cube")
        : nullptr;
    if (Actor == nullptr || Cube == nullptr || !Actor->SetRootComponent(Cube)) return nullptr;
    Cube->SetExtent(Extent);
    Cube->SetWorldTransform(Pico::FTransform(Location));
    Cube->SetPhysicsBodyType(BodyType);
    Cube->SetCollisionEnabled(Pico::ECollisionEnabled::QueryAndPhysics);
    return Cube;
}

bool StatesNearlyEqual(
    const Pico::FCharacterMoveState& Left,
    const Pico::FCharacterMoveState& Right)
{
    return Left.Transform.Translation.Equals(Right.Transform.Translation, 0.01f)
        && Left.Velocity.Equals(Right.Velocity, 0.01f)
        && Left.MovementMode == Right.MovementMode;
}
}

int main()
{
    FTestRunner Runner;
    Pico::FEngineLoop EngineLoop;
    char ProgramName[] = "PicoCharacterMovementTests";
    char Frames[] = "-frames=-1";
    char* Arguments[] = { ProgramName, Frames };
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments) == 0 && EngineLoop.Init() == 0,
        "CharacterMovement tests initialize the Engine and Jolt World");

    Pico::PWorld* World = EngineLoop.GetWorld();
    Pico::PCubeComponent* Floor = World != nullptr
        ? SpawnCube(
            *World,
            "Floor",
            {0.0f, 0.0f, -25.0f},
            {500.0f, 500.0f, 25.0f},
            Pico::EPhysicsBodyType::Static)
        : nullptr;
    Pico::PCubeComponent* Wall = World != nullptr
        ? SpawnCube(
            *World,
            "Wall",
            {300.0f, 0.0f, 100.0f},
            {20.0f, 200.0f, 100.0f},
            Pico::EPhysicsBodyType::Static)
        : nullptr;
    Pico::PCubeComponent* Crate = World != nullptr
        ? SpawnCube(
            *World,
            "Crate",
            {180.0f, 180.0f, 45.0f},
            {45.0f, 45.0f, 45.0f},
            Pico::EPhysicsBodyType::Dynamic)
        : nullptr;
    Pico::PCharacter* Character = World != nullptr
        ? World->SpawnActor<Pico::PCharacter>("Character")
        : nullptr;
    if (Character != nullptr) Character->SetActorLocation({0.0f, 0.0f, 96.0f});

    Runner.Expect(
        World != nullptr && Floor != nullptr && Wall != nullptr && Crate != nullptr
            && Character != nullptr
            && Character->GetCapsuleComponent() != nullptr
            && Character->GetCharacterMovement() != nullptr,
        "Character materializes Capsule and CharacterMovement default subobjects");
    if (World == nullptr || Character == nullptr || Character->GetCharacterMovement() == nullptr)
    {
        EngineLoop.Exit();
        return Runner.Finish();
    }

    auto* Movement = Character->GetCharacterMovement();
    World->Tick(1.0f / 60.0f);
    Runner.Expect(
        Movement->GetMovementMode() == Pico::EMovementMode::Walking
            && Movement->GetCurrentFloor().bWalkableFloor,
        "Character enters Walking when its capsule finds a walkable floor");

    Pico::PCharacter* CollisionMover =
        World->SpawnActor<Pico::PCharacter>("CollisionMover");
    Pico::PCharacter* CollisionBlocker =
        World->SpawnActor<Pico::PCharacter>("CollisionBlocker");
    if (CollisionMover != nullptr && CollisionBlocker != nullptr)
    {
        CollisionMover->SetActorLocation({-100.0f, 300.0f, 96.0f});
        CollisionBlocker->SetActorLocation({100.0f, 300.0f, 96.0f});
        CollisionMover->GetCharacterMovement()->SetMovementMode(
            Pico::EMovementMode::Walking);
        CollisionBlocker->GetCharacterMovement()->SetMovementMode(
            Pico::EMovementMode::Walking);
    }
    Pico::FCharacterMoveInput PawnCollisionInput;
    PawnCollisionInput.WorldInput = Pico::FVector3(1.0f, 0.0f, 0.0f);
    for (int Step = 0; CollisionMover != nullptr && Step < 120; ++Step)
    {
        CollisionMover->GetCharacterMovement()->SimulateMovement(
            PawnCollisionInput, 1.0f / 60.0f);
    }
    const float BlockingPawnX = CollisionBlocker != nullptr
        ? CollisionBlocker->GetActorLocation().X : 0.0f;
    Runner.Expect(
        CollisionMover != nullptr && CollisionBlocker != nullptr
            && CollisionMover->GetActorLocation().X < 30.0f
            && std::abs(BlockingPawnX - 100.0f) < 0.01f,
        "CharacterMovement Pawn profile blocks a second Character without pushing it");
    if (CollisionMover != nullptr && CollisionBlocker != nullptr)
    {
        CollisionBlocker->GetCapsuleComponent()->SetCollisionProfile(
            Pico::ECollisionProfile::PawnNoPawnCollision);
        Pico::FCharacterMoveState ResetState;
        ResetState.Transform = Pico::FTransform({-100.0f, 300.0f, 96.0f});
        ResetState.MovementMode = Pico::EMovementMode::Walking;
        CollisionMover->GetCharacterMovement()->ApplyMoveState(ResetState);
        for (int Step = 0; Step < 120; ++Step)
        {
            CollisionMover->GetCharacterMovement()->SimulateMovement(
                PawnCollisionInput, 1.0f / 60.0f);
        }
    }
    Runner.Expect(
        CollisionMover != nullptr
            && CollisionMover->GetActorLocation().X > 120.0f,
        "Pawn Ignore Pawns profile allows CharacterMovement to pass while still using floor collision");

    Pico::FHitResult FlatHit;
    FlatHit.bBlockingHit = true;
    FlatHit.ImpactNormal = Pico::FVector3::UpVector;
    Pico::FHitResult SteepHit = FlatHit;
    SteepHit.ImpactNormal = Pico::FVector3(0.98f, 0.0f, 0.2f).GetSafeNormal();
    Runner.Expect(
        Movement->IsWalkable(FlatHit) && !Movement->IsWalkable(SteepHit),
        "Walkable floor angle accepts flat ground and rejects a steep surface");

    const float WalkStartX = Character->GetActorLocation().X;
    Pico::FCharacterMoveInput WalkInput;
    WalkInput.WorldInput = Pico::FVector3(1.0f, 0.0f, 0.0f);
    for (int Step = 0; Step < 10; ++Step)
    {
        Movement->SimulateMovement(WalkInput, 1.0f / 60.0f);
    }
    Runner.Expect(
        Character->GetActorLocation().X > WalkStartX
            && Movement->GetMovementMode() == Pico::EMovementMode::Walking,
        "Walking accelerates across the floor through MoveComponent Sweep");

    Pico::FCharacterMoveInput JumpInput;
    JumpInput.bJumpPressed = true;
    const float JumpStartZ = Character->GetActorLocation().Z;
    Movement->SimulateMovement(JumpInput, 1.0f / 60.0f);
    Runner.Expect(
        Movement->GetMovementMode() == Pico::EMovementMode::Falling
            && Movement->GetVelocity().Z > 0.0f
            && Character->GetActorLocation().Z > JumpStartZ,
        "Jump changes Walking to Falling and applies upward velocity");

    const float JumpVelocity = Movement->GetVelocity().Z;
    Movement->SimulateMovement(JumpInput, 1.0f / 60.0f);
    Runner.Expect(
        Movement->GetVelocity().Z < JumpVelocity + 1.0f,
        "Jump input cannot restart a jump while already Falling");

    bool bObservedDescending = false;
    bool bLanded = false;
    for (int Step = 0; Step < 180; ++Step)
    {
        Movement->SimulateMovement({}, 1.0f / 60.0f);
        bObservedDescending |= Movement->GetVelocity().Z < 0.0f;
        if (Movement->GetMovementMode() == Pico::EMovementMode::Walking)
        {
            bLanded = true;
            break;
        }
    }
    Runner.Expect(
        bObservedDescending && bLanded
            && std::abs(Character->GetActorLocation().Z - 96.0f) < 1.0f,
        "Falling applies gravity and returns to Walking on landing");

    Pico::FCharacterMoveState EdgeState = Movement->CaptureMoveState();
    EdgeState.Transform.Translation = {620.0f, 0.0f, 96.0f};
    EdgeState.Velocity = Pico::FVector3::ZeroVector;
    EdgeState.MovementMode = Pico::EMovementMode::Walking;
    Movement->ApplyMoveState(EdgeState);
    Movement->SimulateMovement({}, 1.0f / 60.0f);
    Runner.Expect(
        Movement->GetMovementMode() == Pico::EMovementMode::Falling,
        "Walking transitions to Falling after leaving the floor edge");

    Pico::FCharacterMoveState ReplayStart = EdgeState;
    ReplayStart.Transform.Translation = {-200.0f, -200.0f, 96.0f};
    ReplayStart.MovementMode = Pico::EMovementMode::Walking;
    Movement->ApplyMoveState(ReplayStart);
    for (int Step = 0; Step < 20; ++Step)
    {
        Movement->SimulateMovement(WalkInput, 1.0f / 60.0f);
    }
    const Pico::FCharacterMoveState FirstReplay = Movement->CaptureMoveState();
    Movement->ApplyMoveState(ReplayStart);
    for (int Step = 0; Step < 20; ++Step)
    {
        Movement->SimulateMovement(WalkInput, 1.0f / 60.0f);
    }
    Runner.Expect(
        StatesNearlyEqual(FirstReplay, Movement->CaptureMoveState()),
        "The same Character state and input sequence replay to the same result");

    Pico::PController* Controller = World->SpawnActor<Pico::PController>("Controller");
    Movement->ApplyMoveState(ReplayStart);
    Character->SetActorRotation({0.0f, 170.0f, 0.0f});
    Movement->SetOrientRotationToMovement(false);
    Movement->SetUseControllerDesiredRotation(true);
    Movement->SetRotationRate(60.0f);
    if (Controller != nullptr)
    {
        Controller->Possess(Character);
        Controller->SetControlRotation({0.0f, -170.0f, 0.0f});
    }
    Movement->SimulateMovement({}, 0.1f);
    Runner.Expect(
        Controller != nullptr
            && Character->GetActorRotation().Yaw > 170.0f
            && Character->GetActorRotation().Yaw < 180.0f,
        "Controller desired rotation works without movement input and follows the shortest yaw path");
    Character->SetUseControllerRotationYaw(true);
    Controller->SetControlRotation({0.0f, 45.0f, 0.0f});
    Movement->SimulateMovement({}, 1.0f / 60.0f);
    Runner.Expect(
        Character->GetActorRotation().Equals({0.0f, 45.0f, 0.0f}, 0.01f),
        "Pawn controller yaw directly drives actor yaw when enabled");
    Character->SetUseControllerRotationYaw(false);
    Movement->SetUseControllerDesiredRotation(false);
    Movement->SetOrientRotationToMovement(true);
    Movement->SetRotationRate(540.0f);

    Movement->ApplyMoveState(ReplayStart);
    Movement->SimulateMovement(WalkInput, 1.0f);
    Runner.Expect(
        Movement->GetLastSimulationIterations() == Movement->GetMaxSimulationIterations()
            && Character->GetActorLocation().X - ReplayStart.Transform.Translation.X
                <= Movement->GetMaxWalkSpeed()
                    * Movement->GetMaxSimulationDeltaTime()
                    * static_cast<float>(Movement->GetMaxSimulationIterations())
                    + 1.0f,
        "Long frames are capped by bounded Character simulation iterations");

    Pico::FCharacterMoveState RootMotionStart = ReplayStart;
    RootMotionStart.Transform.Translation = {200.0f, 0.0f, 96.0f};
    Movement->ApplyMoveState(RootMotionStart);
    Pico::FCharacterMoveInput RootMotionInput;
    RootMotionInput.RootMotionDelta.Translation = {200.0f, 0.0f, 0.0f};
    Movement->SimulateMovement(RootMotionInput, 1.0f / 60.0f);
    Runner.Expect(
        Character->GetActorLocation().X < 260.0f
            && Movement->GetLastSimulationInput().RootMotionDelta.Translation.X == 200.0f,
        "Root motion uses swept Character movement and remains part of replayable move input");

    Pico::FCharacterMoveState PushState = ReplayStart;
    PushState.Transform.Translation = {40.0f, 180.0f, 96.0f};
    Movement->ApplyMoveState(PushState);
    for (int Step = 0; Step < 40; ++Step)
    {
        Movement->SimulateMovement(WalkInput, 1.0f / 60.0f);
    }
    World->Tick(1.0f / 60.0f);
    Pico::FPhysicsBodyState CrateState;
    Runner.Expect(
        Crate->GetPhysicsBodyHandle().IsValid()
            && World->GetPhysicsScene()->GetBodyState(
                Crate->GetPhysicsBodyHandle(), CrateState)
            && CrateState.LinearVelocity.X > 0.0f,
        "Character impact pushes a Dynamic body through the physics interface");

    Pico::PCubeComponent* NoPushCrate = SpawnCube(
        *World,
        "NoPushCrate",
        {180.0f, -180.0f, 45.0f},
        {45.0f, 45.0f, 45.0f},
        Pico::EPhysicsBodyType::Dynamic);
    Movement->SetPhysicsInteractionEnabled(false);
    PushState.Transform.Translation = {40.0f, -180.0f, 96.0f};
    PushState.Velocity = Pico::FVector3::ZeroVector;
    PushState.MovementMode = Pico::EMovementMode::Walking;
    Movement->ApplyMoveState(PushState);
    for (int Step = 0; Step < 40; ++Step)
    {
        Movement->SimulateMovement(WalkInput, 1.0f / 60.0f);
    }
    World->Tick(1.0f / 60.0f);
    Pico::FPhysicsBodyState NoPushState;
    Runner.Expect(
        NoPushCrate != nullptr
            && !Movement->IsPhysicsInteractionEnabled()
            && World->GetPhysicsScene()->GetBodyState(
                NoPushCrate->GetPhysicsBodyHandle(), NoPushState)
            && std::abs(NoPushState.LinearVelocity.X) < 0.01f,
        "Disabled physics interaction preserves Sweep blocking without pushing a Dynamic body");
    Movement->SetPhysicsInteractionEnabled(true);

    Pico::FCharacterMoveState NoCollisionStart = Movement->CaptureMoveState();
    NoCollisionStart.Transform.Translation = {200.0f, 0.0f, 96.0f};
    NoCollisionStart.Velocity = Pico::FVector3::ZeroVector;
    NoCollisionStart.MovementMode = Pico::EMovementMode::Falling;
    Movement->ApplyMoveState(NoCollisionStart);
    Character->GetCapsuleComponent()->SetCollisionEnabled(
        Pico::ECollisionEnabled::NoCollision);
    Pico::FCharacterMoveInput NoCollisionMove;
    NoCollisionMove.RootMotionDelta.Translation = {200.0f, 0.0f, 0.0f};
    Movement->SimulateMovement(NoCollisionMove, 1.0f / 60.0f);
    Runner.Expect(
        Character->GetActorLocation().X > 390.0f
            && !Movement->GetCurrentFloor().bBlockingHit,
        "NoCollision disables Character movement sweeps and floor queries");

    Movement->SetNetworkPolicyHash(0xCAFEu);
    Pico::FCharacterNetworkMove NetworkMove;
    NetworkMove.Sequence = 1;
    NetworkMove.DeltaSeconds = 1.0f / 60.0f;
    NetworkMove.Input.WorldInput = {1.0f, 0.0f, 0.0f};
    NetworkMove.PolicyHash = 0xCAFEu;
    Runner.Expect(
        Movement->EnqueueServerMove(NetworkMove)
            && Movement->EnqueueServerMove(NetworkMove)
            && Movement->GetPredictionStatistics().ServerMoveQueueCount == 1,
        "Server SavedMove queue accepts one move and deduplicates retransmission");
    NetworkMove.Sequence = 2;
    NetworkMove.PolicyHash = 0xBADu;
    Runner.Expect(
        !Movement->EnqueueServerMove(NetworkMove)
            && Movement->GetPredictionStatistics().ServerMoveQueueCount == 1,
        "Server rejects movement produced by a different control policy hash");

    auto* NetworkVisual =
        Character->CreateComponent<Pico::PSkeletalMeshComponent>(
            "NetworkSmoothingVisual");
    if (NetworkVisual != nullptr)
    {
        NetworkVisual->AttachToComponent(
            Character->GetRootComponent(),
            Pico::EAttachmentTransformRule::KeepRelative);
    }
    Pico::FCharacterMoveState SmoothStart = Movement->CaptureMoveState();
    SmoothStart.Transform.Translation = {-300.0f, -350.0f, 96.0f};
    SmoothStart.Velocity = Pico::FVector3::ZeroVector;
    SmoothStart.MovementMode = Pico::EMovementMode::None;
    Movement->ApplyMoveState(SmoothStart);
    Pico::FCharacterNetworkState SmoothSnapshot;
    SmoothSnapshot.ServerTick = 1;
    SmoothSnapshot.State = SmoothStart;
    SmoothSnapshot.State.Transform.Translation.X += 100.0f;

    Movement->SetNetworkSmoothingMode(
        Pico::ENetworkSmoothingMode::Disabled);
    Movement->ReceiveSimulatedSnapshot(SmoothSnapshot);
    Runner.Expect(
        NetworkVisual != nullptr
            && Character->GetActorLocation().X
                == SmoothSnapshot.State.Transform.Translation.X
            && !NetworkVisual->HasNetworkSmoothingVisualTransform(),
        "Disabled network smoothing applies the authoritative snapshot immediately");

    Movement->ApplyMoveState(SmoothStart);
    Movement->SetNetworkSmoothingMode(Pico::ENetworkSmoothingMode::Linear);
    Movement->SetNetworkSimulatedSmoothLocationTime(0.1f);
    SmoothSnapshot.ServerTick = 2;
    const float LinearOldVisualX = NetworkVisual != nullptr
        ? NetworkVisual->GetVisualWorldTransform().Translation.X : 0.0f;
    Movement->ReceiveSimulatedSnapshot(SmoothSnapshot);
    Runner.Expect(
        NetworkVisual != nullptr
            && Character->GetActorLocation().X
                == SmoothSnapshot.State.Transform.Translation.X
            && NetworkVisual->HasNetworkSmoothingVisualTransform()
            && std::abs(
                NetworkVisual->GetVisualWorldTransform().Translation.X
                    - LinearOldVisualX) < 0.01f,
        "Linear smoothing updates the authoritative capsule while preserving the old Mesh visual");
    Movement->TickComponent(0.1f);
    Runner.Expect(
        NetworkVisual != nullptr
            && !NetworkVisual->HasNetworkSmoothingVisualTransform()
            && std::abs(
                NetworkVisual->GetVisualWorldTransform().Translation.X
                    - SmoothSnapshot.State.Transform.Translation.X) < 0.01f,
        "Linear smoothing reaches the authoritative Mesh transform in its configured time");

    Movement->ApplyMoveState(SmoothStart);
    Movement->SetNetworkSmoothingMode(
        Pico::ENetworkSmoothingMode::Exponential);
    SmoothSnapshot.ServerTick = 3;
    const float ExponentialOldVisualX = NetworkVisual != nullptr
        ? NetworkVisual->GetVisualWorldTransform().Translation.X : 0.0f;
    Movement->ReceiveSimulatedSnapshot(SmoothSnapshot);
    Movement->TickComponent(0.05f);
    const float ExponentialVisualX = NetworkVisual != nullptr
        ? NetworkVisual->GetVisualWorldTransform().Translation.X : 0.0f;
    Runner.Expect(
        NetworkVisual != nullptr
            && NetworkVisual->HasNetworkSmoothingVisualTransform()
            && ExponentialVisualX > ExponentialOldVisualX
            && ExponentialVisualX
                < SmoothSnapshot.State.Transform.Translation.X,
        "Exponential smoothing immediately begins decaying the Mesh offset without delaying the capsule");

    Pico::FCharacterNetworkState MovingSnapshot;
    MovingSnapshot.ServerTick = 4;
    MovingSnapshot.State = Movement->CaptureMoveState();
    MovingSnapshot.State.Transform.Translation = {-300.0f, -350.0f, 96.0f};
    MovingSnapshot.State.Velocity = {100.0f, 0.0f, 0.0f};
    MovingSnapshot.State.MovementMode = Pico::EMovementMode::Walking;
    Movement->SetSimulatedProxyExtrapolationEnabled(true);
    Movement->SetNetworkMaxSimulatedProxyExtrapolationTime(0.2f);
    Movement->ReceiveSimulatedSnapshot(MovingSnapshot);
    Movement->SimulateProxyMovement(0.05f);
    Runner.Expect(
        std::abs(Character->GetActorLocation().X
            - (MovingSnapshot.State.Transform.Translation.X + 5.0f)) < 0.1f,
        "Simulated proxy advances between authoritative snapshots using replicated velocity");
    Movement->SimulateProxyMovement(0.5f);
    const Pico::FCharacterPredictionStatistics ExtrapolationStats =
        Movement->GetPredictionStatistics();
    Runner.Expect(
        std::abs(Character->GetActorLocation().X
            - (MovingSnapshot.State.Transform.Translation.X + 20.0f)) < 0.1f
            && std::abs(
                ExtrapolationStats.SimulatedProxyExtrapolationSeconds - 0.2f)
                < 0.001f
            && ExtrapolationStats.SimulatedProxyExtrapolationClampCount == 1,
        "Simulated proxy extrapolation stops at the configured safety horizon");

    Movement->SetNetworkSmoothingMode(Pico::ENetworkSmoothingMode::Exponential);
    Movement->SetUseAdaptiveNetworkSmoothing(true);
    Movement->SetNetworkMinAdaptiveSmoothTime(0.025f);
    Movement->SetNetworkMaxAdaptiveSmoothTime(0.1f);
    Pico::FCharacterNetworkState TimedSnapshot = MovingSnapshot;
    TimedSnapshot.ServerTick = 5;
    TimedSnapshot.ServerTimeSeconds = 1.0;
    TimedSnapshot.State.Velocity = Pico::FVector3::ZeroVector;
    TimedSnapshot.State.MovementMode = Pico::EMovementMode::None;
    Character->GetWorld()->Tick(1.0f / 60.0f);
    Movement->ReceiveSimulatedSnapshot(TimedSnapshot);
    TimedSnapshot.ServerTick = 6;
    TimedSnapshot.ServerTimeSeconds += 1.0 / 60.0;
    Character->GetWorld()->Tick(1.0f / 60.0f);
    Movement->ReceiveSimulatedSnapshot(TimedSnapshot);
    const Pico::FCharacterPredictionStatistics TimingStats =
        Movement->GetPredictionStatistics();
    Runner.Expect(
        TimingStats.SnapshotReceiveIntervalSeconds > 0.015f
            && TimingStats.SnapshotReceiveIntervalSeconds < 0.018f
            && TimingStats.SnapshotServerIntervalSeconds > 0.015f
            && TimingStats.SnapshotServerIntervalSeconds < 0.018f
            && TimingStats.EffectiveNetworkSmoothingTimeSeconds >= 0.025f
            && TimingStats.EffectiveNetworkSmoothingTimeSeconds < 0.05f,
        "Adaptive smoothing derives a bounded low-latency window from stable snapshot timing");

    Movement->SetNetworkSmoothingMode(
        Pico::ENetworkSmoothingMode::SnapshotInterpolation);

    for (Pico::uint32 Tick = 7; Tick <= 46; ++Tick)
    {
        Pico::FCharacterNetworkState Snapshot;
        Snapshot.ServerTick = Tick;
        Snapshot.State = Movement->CaptureMoveState();
        Snapshot.State.Transform.Translation.X += static_cast<float>(Tick);
        Movement->ReceiveSimulatedSnapshot(Snapshot);
    }
    Runner.Expect(
        Movement->GetPredictionStatistics().SnapshotCount == 32,
        "Simulated proxy interpolation history remains bounded under sustained snapshots");

    Pico::PCharacter* JumpAuthority =
        World->SpawnActor<Pico::PCharacter>("JumpAuthority");
    Pico::PCharacter* JumpProxy =
        World->SpawnActor<Pico::PCharacter>("JumpProxy");
    auto* JumpVisual = JumpProxy != nullptr
        ? JumpProxy->CreateComponent<Pico::PSkeletalMeshComponent>("JumpVisual")
        : nullptr;
    if (JumpVisual != nullptr)
    {
        JumpVisual->AttachToComponent(
            JumpProxy->GetRootComponent(),
            Pico::EAttachmentTransformRule::KeepRelative);
    }
    auto* AuthorityMovement = JumpAuthority != nullptr
        ? JumpAuthority->GetCharacterMovement() : nullptr;
    auto* ProxyMovement = JumpProxy != nullptr
        ? JumpProxy->GetCharacterMovement() : nullptr;
    Pico::FCharacterMoveState AuthorityStart;
    AuthorityStart.Transform.Translation = {150.0f, -300.0f, 96.0f};
    AuthorityStart.MovementMode = Pico::EMovementMode::Walking;
    Pico::FCharacterMoveState ProxyStart = AuthorityStart;
    ProxyStart.Transform.Translation.X = -150.0f;
    if (AuthorityMovement != nullptr) AuthorityMovement->ApplyMoveState(AuthorityStart);
    if (ProxyMovement != nullptr)
    {
        ProxyMovement->ApplyMoveState(ProxyStart);
        ProxyMovement->SetNetworkSmoothingMode(
            Pico::ENetworkSmoothingMode::Exponential);
        ProxyMovement->SetUseAdaptiveNetworkSmoothing(true);
    }

    constexpr float JumpStep = 1.0f / 60.0f;
    std::deque<Pico::FCharacterNetworkState> DelayedJumpSnapshots;
    float AuthorityPeakZ = AuthorityStart.Transform.Translation.Z;
    float ProxyRootPeakZ = ProxyStart.Transform.Translation.Z;
    float ProxyVisualPeakZ = ProxyStart.Transform.Translation.Z;
    Pico::EMovementMode PreviousProxyMode = Pico::EMovementMode::Walking;
    int ProxyFallingEntryCount = 0;
    for (Pico::uint32 Frame = 0; Frame < 120
        && AuthorityMovement != nullptr && ProxyMovement != nullptr; ++Frame)
    {
        Pico::FCharacterMoveInput DelayedJumpInput;
        DelayedJumpInput.bJumpPressed = Frame == 0;
        AuthorityMovement->SimulateMovement(DelayedJumpInput, JumpStep);
        Pico::FCharacterNetworkState Snapshot;
        Snapshot.ServerTick = 100 + Frame;
        Snapshot.ServerTimeSeconds = static_cast<double>(Frame) * JumpStep;
        Snapshot.State = AuthorityMovement->CaptureMoveState();
        AuthorityPeakZ = std::max(
            AuthorityPeakZ, Snapshot.State.Transform.Translation.Z);
        DelayedJumpSnapshots.push_back(Snapshot);
        if (DelayedJumpSnapshots.size() > 2)
        {
            Pico::FCharacterNetworkState Delivered =
                DelayedJumpSnapshots.front();
            DelayedJumpSnapshots.pop_front();
            Delivered.State.Transform.Translation.X = -150.0f;
            ProxyMovement->ReceiveSimulatedSnapshot(Delivered);
            ProxyMovement->SimulateProxyMovement(JumpStep);
            ProxyMovement->SmoothClientPosition(JumpStep);
        }
        const Pico::EMovementMode CurrentProxyMode =
            ProxyMovement->GetMovementMode();
        if (PreviousProxyMode != Pico::EMovementMode::Falling
            && CurrentProxyMode == Pico::EMovementMode::Falling)
        {
            ++ProxyFallingEntryCount;
        }
        PreviousProxyMode = CurrentProxyMode;
        ProxyRootPeakZ = std::max(
            ProxyRootPeakZ, JumpProxy->GetActorLocation().Z);
        if (JumpVisual != nullptr)
        {
            ProxyVisualPeakZ = std::max(
                ProxyVisualPeakZ,
                JumpVisual->GetVisualWorldTransform().Translation.Z);
        }
    }
    Runner.Expect(
        JumpVisual != nullptr
            && ProxyRootPeakZ <= AuthorityPeakZ + 10.0f
            && ProxyVisualPeakZ <= AuthorityPeakZ + 10.0f,
        "Delayed jump snapshots do not create a higher proxy or Mesh double-jump peak");
    Runner.Expect(
        ProxyFallingEntryCount == 1,
        "One authoritative jump enters Falling only once on a delayed simulated proxy");

    Pico::PCharacter* SoakAuthority =
        World->SpawnActor<Pico::PCharacter>("SoakAuthority");
    Pico::PCharacter* SoakProxy =
        World->SpawnActor<Pico::PCharacter>("SoakProxy");
    auto* SoakVisual = SoakProxy != nullptr
        ? SoakProxy->CreateComponent<Pico::PSkeletalMeshComponent>("SoakVisual")
        : nullptr;
    if (SoakVisual != nullptr)
    {
        SoakVisual->AttachToComponent(
            SoakProxy->GetRootComponent(),
            Pico::EAttachmentTransformRule::KeepRelative);
    }
    auto* SoakAuthorityMovement = SoakAuthority != nullptr
        ? SoakAuthority->GetCharacterMovement() : nullptr;
    auto* SoakProxyMovement = SoakProxy != nullptr
        ? SoakProxy->GetCharacterMovement() : nullptr;
    Pico::FCharacterMoveState SoakAuthorityStart;
    SoakAuthorityStart.Transform.Translation = {0.0f, -300.0f, 96.0f};
    SoakAuthorityStart.MovementMode = Pico::EMovementMode::Walking;
    Pico::FCharacterMoveState SoakProxyStart = SoakAuthorityStart;
    SoakProxyStart.Transform.Translation.Y = 300.0f;
    if (SoakAuthorityMovement != nullptr)
    {
        SoakAuthorityMovement->ApplyMoveState(SoakAuthorityStart);
        SoakAuthorityMovement->SetMaxWalkSpeed(60.0f);
    }
    if (SoakProxyMovement != nullptr)
    {
        SoakProxyMovement->ApplyMoveState(SoakProxyStart);
        SoakProxyMovement->SetMaxWalkSpeed(60.0f);
        SoakProxyMovement->SetNetworkSmoothingMode(
            Pico::ENetworkSmoothingMode::Exponential);
        SoakProxyMovement->SetUseAdaptiveNetworkSmoothing(true);
    }

    struct FDelayedSoakSnapshot
    {
        Pico::uint32 DeliveryFrame = 0;
        Pico::FCharacterNetworkState State;
    };
    constexpr Pico::uint32 SoakFrames = 10 * 60 * 60;
    constexpr Pico::uint32 OneWayDelayFrames = 5;
    constexpr float SoakStep = 1.0f / 60.0f;
    std::deque<FDelayedSoakSnapshot> SoakSnapshots;
    Pico::uint32 RandomState = 0x5EED1234u;
    std::size_t MaxDelayedSnapshotCount = 0;
    float MaxSoakVisualOffset = 0.0f;
    float SoakMoveDirection = 1.0f;
    for (Pico::uint32 Frame = 0; Frame < SoakFrames
        && SoakAuthorityMovement != nullptr && SoakProxyMovement != nullptr;
        ++Frame)
    {
        const float AuthorityX = SoakAuthority->GetActorLocation().X;
        if (AuthorityX >= 100.0f) SoakMoveDirection = -1.0f;
        else if (AuthorityX <= -100.0f) SoakMoveDirection = 1.0f;
        Pico::FCharacterMoveInput Input;
        Input.WorldInput.X = SoakMoveDirection * 0.5f;
        Input.bJumpPressed = Frame % 600u == 0u;
        SoakAuthorityMovement->SimulateMovement(Input, SoakStep);

        RandomState = RandomState * 1664525u + 1013904223u;
        const bool bDropSnapshot = RandomState % 100u < 5u;
        if (!bDropSnapshot)
        {
            FDelayedSoakSnapshot Delayed;
            Delayed.DeliveryFrame = Frame + OneWayDelayFrames;
            Delayed.State.ServerTick = 1000u + Frame;
            Delayed.State.ServerTimeSeconds =
                static_cast<double>(Frame) * SoakStep;
            Delayed.State.State = SoakAuthorityMovement->CaptureMoveState();
            Delayed.State.State.Transform.Translation.Y += 600.0f;
            SoakSnapshots.push_back(Delayed);
        }
        while (!SoakSnapshots.empty()
            && SoakSnapshots.front().DeliveryFrame <= Frame)
        {
            SoakProxyMovement->ReceiveSimulatedSnapshot(
                SoakSnapshots.front().State);
            SoakSnapshots.pop_front();
        }
        SoakProxyMovement->SimulateProxyMovement(SoakStep);
        SoakProxyMovement->SmoothClientPosition(SoakStep);
        MaxDelayedSnapshotCount = std::max(
            MaxDelayedSnapshotCount, SoakSnapshots.size());
        MaxSoakVisualOffset = std::max(
            MaxSoakVisualOffset,
            SoakProxyMovement->GetNetworkSmoothingVisualOffsetDistance());
    }

    for (Pico::uint32 Frame = 0; Frame < 240u
        && SoakAuthorityMovement != nullptr && SoakProxyMovement != nullptr;
        ++Frame)
    {
        SoakAuthorityMovement->SimulateMovement({}, SoakStep);
        Pico::FCharacterNetworkState FinalState;
        FinalState.ServerTick = 1000u + SoakFrames + Frame;
        FinalState.ServerTimeSeconds =
            static_cast<double>(SoakFrames + Frame) * SoakStep;
        FinalState.State = SoakAuthorityMovement->CaptureMoveState();
        FinalState.State.Transform.Translation.Y += 600.0f;
        SoakProxyMovement->ReceiveSimulatedSnapshot(FinalState);
        SoakProxyMovement->SimulateProxyMovement(SoakStep);
        SoakProxyMovement->SmoothClientPosition(SoakStep);
    }
    Pico::FCharacterMoveState ExpectedSoakState =
        SoakAuthorityMovement != nullptr
        ? SoakAuthorityMovement->CaptureMoveState() : Pico::FCharacterMoveState{};
    ExpectedSoakState.Transform.Translation.Y += 600.0f;
    const Pico::FCharacterMoveState ActualSoakState =
        SoakProxyMovement != nullptr
        ? SoakProxyMovement->CaptureMoveState() : Pico::FCharacterMoveState{};
    Runner.Expect(
        MaxDelayedSnapshotCount <= OneWayDelayFrames,
        "Ten-minute network soak keeps the delayed snapshot queue bounded");
    Runner.Expect(
        SoakVisual != nullptr
            && MaxSoakVisualOffset
                <= SoakProxyMovement->GetNetworkMaxSmoothUpdateDistance()
                    + 0.01f,
        "Ten-minute network soak keeps Mesh smoothing within its configured bound");
    const Pico::FVector3 SoakPositionError =
        ActualSoakState.Transform.Translation
        - ExpectedSoakState.Transform.Translation;
    const std::string SoakConvergenceDescription =
        "Ten-minute network soak converges to the final authoritative position"
        " error=(" + std::to_string(SoakPositionError.X)
        + "," + std::to_string(SoakPositionError.Y)
        + "," + std::to_string(SoakPositionError.Z) + ")"
        + " authority=(" + std::to_string(
            ExpectedSoakState.Transform.Translation.X)
        + "," + std::to_string(ExpectedSoakState.Transform.Translation.Z)
        + ") proxy=(" + std::to_string(
            ActualSoakState.Transform.Translation.X)
        + "," + std::to_string(ActualSoakState.Transform.Translation.Z)
        + ")";
    Runner.Expect(
        ActualSoakState.Transform.Translation.Equals(
            ExpectedSoakState.Transform.Translation, 5.0f),
        SoakConvergenceDescription);
    Runner.Expect(
        SoakProxyMovement != nullptr
            && SoakProxyMovement->GetPredictionStatistics().SnapshotCount <= 32,
        "Ten-minute network soak keeps proxy snapshot history bounded");

    EngineLoop.Exit();
    return Runner.Finish();
}
