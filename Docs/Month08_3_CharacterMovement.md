# Month 8.3: Character Movement

This milestone builds a small UE-inspired Character layer on Pico's common movement and physics
boundaries. It supports deterministic walking, jumping, falling, landing, wall sliding, floor loss,
and basic dynamic-body pushing without exposing Jolt types to Gameplay.

## Object Chain

```text
PController
  -> Possess(PCharacter)
       -> PCapsuleComponent                 collision root
       -> PCharacterMovementComponent       movement policy
            -> MoveUpdatedComponent
                 -> PSceneComponent::MoveComponent
                      -> IWorldCollisionQuery::Sweep
                           -> PicoPhysicsJolt
```

`PCharacter` derives from `PPawn`. Its CDO declares an inherited `CollisionCapsule` and
`CharacterMovement` default-subobject template. A runtime Character receives independent component
instances through the normal ObjectInitializer chain. Project classes can request the same names to
customize those inherited templates instead of creating a second capsule or movement component.

The Character owns only the one-shot jump request. CharacterMovement owns movement mode, velocity,
floor state, tuning values, and simulation. The capsule remains kinematic and QueryOnly: Gameplay
moves it by Sweep, while dynamic bodies remain authoritative in Jolt.

## Deterministic Simulation Boundary

The reusable input and state are plain Pico data:

```text
FCharacterMoveInput = WorldInput + JumpPressed
FCharacterMoveState = Transform + Velocity + MovementMode
```

`CaptureMoveState`, `ApplyMoveState`, and `SimulateMovement` form the future network-prediction
boundary. Applying the same state and input for the same delta produces the same Gameplay result.
No pointer, local ObjectHandle, Jolt BodyID, or frame-global input state is part of that record.

Large frame deltas are split by `MaxSimulationDeltaTime` and bounded by
`MaxSimulationIterations`. This prevents one Character tick from entering an unlimited catch-up
loop. It is not yet a complete network move protocol; sequence numbers, saved moves, acknowledgement,
server correction, and client replay belong to the networking milestone.

## Movement Modes

`Walking` performs these operations:

1. Flatten and normalize world-space movement input.
2. Project desired movement onto the current walkable floor plane.
3. Accelerate toward `MaxWalkSpeed`, or brake toward zero with no input.
4. Sweep the capsule, report impacts, push a dynamic body, and slide along blocking walls.
5. Probe downward again; switch to `Falling` when no walkable floor remains.

`Falling` applies horizontal air control and Pico gravity, then sweeps the capsule. A downward hit
whose normal satisfies `WalkableFloorAngle` changes the mode to `Walking` and clears vertical
velocity. A jump is consumed only while Walking, gives one upward `JumpZVelocity`, and immediately
enters Falling, so holding the key cannot repeatedly jump in mid-air.

`FindFloor` uses the capsule shape and the World's backend-neutral Sweep API. `IsWalkable` compares
the impact normal against the configured floor angle. Step-up, crouch, moving bases, swimming,
flying, nav walking, and arbitrary custom modes are deliberately deferred.

## Physics Timing

Jolt now advances at a fixed 60 Hz. Each World frame accumulates elapsed time and executes at most
four fixed substeps. Excess accumulated time is discarded to avoid a slow frame creating an
unbounded spiral of catch-up work. `PWorld::GetPhysicsStepCount` records actual Jolt updates, not
rendered World frames.

Character movement still runs in `PrePhysics`: its kinematic Sweep can push a dynamic body before
the fixed Jolt update. Dynamic-body transforms are written back after Jolt Step, then contact events
are dispatched before `PostPhysics`.

## Sandbox Verification

Build and run:

```powershell
cmake --build Build --config Debug --parallel
.\Build\Debug\PicoSandboxGame.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

Press `F1` and verify:

1. At rest, `Character mode` is `Walking`, `On ground` is `yes`, and Floor is walkable. This proves
   the inherited capsule exists, downward Sweep finds the floor, and the mode transition completed.
2. Hold `W/A/S/D`. Velocity and Last delta change while `Pending input` returns to zero after each
   tick. This proves Controller input is consumed once by CharacterMovement and reaches the shared
   MoveComponent path.
3. Walk into a blue wall. The Pawn stops or slides and `Blocking hit` can report `yes`. This proves
   capsule Sweep, impact handling, and wall projection are active.
4. Press `Space`. Mode changes to `Falling`, vertical velocity becomes positive, then negative, and
   finally returns to Walking on landing. This proves one-shot jump, gravity, floor recognition, and
   landing state changes.
5. Hold Space while airborne. The Character must not jump again. This proves the jump request is
   consumed and Walking is required for another jump.
6. Walk off an edge. Mode changes to Falling without pressing Space. This proves floor loss, rather
   than the jump button, controls unsupported movement.
7. Walk into the red dynamic cube. It should receive an impulse and move. This proves Character
   impact code talks through `PPrimitiveComponent::AddImpulse`, while Jolt remains the dynamic-body
   authority.
8. Watch `Simulation iterations`. Normal frames usually use one iteration; an artificial long frame
   remains bounded. Physics `Steps` may advance by zero or several updates per render frame, but no
   frame can add more than four.

`R` still asks GameMode to restart the player. The replacement Pawn must again possess a fresh
Character with its own capsule and CharacterMovement instances.

## Automated Verification

`PicoCharacterMovementTests` covers default-subobject construction, floor detection, walkable slope
classification, acceleration, jump, no mid-air double jump, gravity, landing, edge transition,
state/input replay, bounded long frames, and dynamic-body pushing.

`PicoSandboxTests` verifies the project Pawn derives from Character, reuses the inherited default
subobjects, enters Walking, consumes mapped input, jumps through `Action.Jump`, and still works with
GameMode restart. `PicoPhysicsTests` verifies a one-second frame performs only four fixed Jolt
substeps. Existing Movement tests remain independent from Jolt.

## Deliberate Limits

This is a learning-sized CharacterMovement, not a copy of UE's full `UCharacterMovementComponent`.
It does not yet implement step-up, ledge policy, moving platforms, crouch, rotation policy, root
motion, animation pose selection, network roles, compressed saved moves, prediction, correction, or
smoothing. The next animation milestone must read movement state and route Root Motion back through
MovementComponent; it must not write Actor Transform through a separate path.
