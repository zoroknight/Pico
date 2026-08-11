# Month 8.1: Movement Foundation

This milestone establishes Pico's UE-inspired Gameplay movement boundary before Jolt, Character,
animation root motion, and network prediction are added.

## Runtime Chain

```text
PlayerController
 -> Pawn::AddMovementInput
 -> PendingMovementInputVector
 -> PawnMovementComponent::ConsumeInputVector
 -> FloatingPawnMovement::TickComponent
 -> SafeMoveUpdatedComponent
 -> SceneComponent::MoveComponent
 -> IWorldCollisionQuery::Sweep
 -> Relative/World Transform
```

Gameplay no longer sends `DeltaSeconds` to `PSandboxPawn` or writes its final location. The
controller submits a world-space intention, the Pawn stores transient input, and the movement
component consumes it once per frame.

## UE-Inspired Responsibilities

`PSceneComponent::MoveComponent` is the low-level translation and rotation operation. Sweep moves
only to the blocking hit time; an explicit `ETeleportType` bypasses the query. Direct transform
setters remain valid for editor tools, loading, initialization, and deliberate teleportation.

`PMovementComponent` owns an `UpdatedComponent` and exposes `MoveUpdatedComponent`,
`SafeMoveUpdatedComponent`, and `SlideAlongSurface`. `PPawnMovementComponent` connects that layer to
Pawn input. `PFloatingPawnMovement` supplies acceleration, deceleration, maximum speed, velocity,
and collision sliding without gravity.

Possession refreshes an explicit tick prerequisite:

```text
PlayerController::PrimaryActorTick
 -> FloatingPawnMovement::PrimaryComponentTick
```

The prerequisite is established whether possession happens before or after component registration,
and is removed by `UnPossess`.

## Backend Boundary

`PicoPhysicsCore` contains Pico-owned collision descriptions, query parameters, `FHitResult`, and
`IWorldCollisionQuery`. It contains no Jolt headers or IDs. A World temporarily accepts a non-owning
query implementation; World teardown clears the pointer. Month 8.2 will provide the owned physics
scene and Jolt adapter behind this interface.

`FHitResult` stores a stable `FObjectHandle`, never a raw physics-library pointer. Reflection,
serialization, Gameplay, and future network structures therefore remain independent of Jolt.

## Gameplay Debug

Press `F1` in `PicoSandboxGame` to open Gameplay Debug. The Movement section shows:

- MovementComponent and UpdatedComponent identities;
- pending and last-consumed input vectors;
- velocity and last applied movement delta;
- Direct, Sweep, or Teleport mode;
- last blocking-hit state and normalized hit time.

When a blocking move is followed by an unobstructed slide sub-move, diagnostics retain the original
blocking `FHitResult`. Internal movement decomposition therefore cannot make a visibly blocked frame report
`Blocking hit: no` and `Time: 1`.

At this milestone the runtime uses no real physics scene, so ordinary movement reports Sweep mode
without a blocking hit. The important visible sequence is input becoming `Last input`, velocity
changing, the Pawn moving, and velocity decelerating after input is released.

## Automated Acceptance

`PicoMovementTests` covers:

- accumulated, clamped, and single-consumption Pawn input;
- UpdatedComponent ownership and automatic root selection;
- Controller-before-Movement tick prerequisites and UnPossess cleanup;
- proportional Sweep stopping and explicit Teleport bypass;
- stable query handles and Pico collision shapes;
- attachment propagation;
- surface sliding and initial-penetration recovery;
- FloatingPawnMovement acceleration, movement, and deceleration.

`PicoSandboxTests` verifies that the project Pawn materializes root, mesh, and movement default
subobjects and that mapped input is consumed by MovementComponent before the Pawn moves.

## Deliberate Limits

This milestone does not implement Jolt, physical bodies, gravity, jumping, floor detection,
CharacterMovement, or network prediction. Those systems must use this movement boundary rather than
introducing another path that writes the Pawn transform.
