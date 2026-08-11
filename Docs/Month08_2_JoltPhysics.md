# Month 8.2: Jolt Physics Scene

This milestone connects Pico's Month 8.1 movement boundary to a real synchronous physics scene while
keeping Jolt types outside Gameplay, reflection, serialization, and networking data.

## Runtime Frame

`FTickTaskManager` now supports an explicit staged frame:

```text
BeginFrame
 -> PrePhysics       Controller and Movement produce this frame's movement
 -> DuringPhysics    reserved synchronous-physics boundary
 -> Jolt Step
 -> dynamic Body state -> SceneComponent
 -> Hit / BeginOverlap / EndOverlap dispatch
 -> PostPhysics
 -> PostUpdateWork   future animation, camera, and render preparation
 -> EndFrame
```

Tick groups must be run in order. Registrations made during a frame do not execute until the next
frame, and prerequisites still order functions inside a group. The adapter uses a single-threaded
Jolt job system. Month 8.3 subsequently replaced one variable step per World frame with fixed 60 Hz
updates capped at four substeps.

## Module Boundary

```text
Gameplay / Engine
       |
       v
PicoPhysicsCore
  FCollisionShape
  FPhysicsBodyHandle
  FPhysicsBodyDesc / State
  FHitResult / FOverlapResult
  IWorldCollisionQuery
  IPhysicsScene
       ^
       |
PicoPhysicsJolt
  Jolt shape conversion
  BodyID mapping
  Raycast / Sweep / Overlap conversion
  contact listener and Step
       |
       v
Jolt Physics 5.6.0
```

The build pins Jolt commit `e77f175595e64cb44218cc9d9d56fc365ad0e36a`. `BodyID`, Jolt pointers,
headers, and allocator ownership only appear in `PicoPhysicsJolt`. Public state uses Pico math types and
stable handles. Jolt positions and lengths are converted between meters and Pico centimeters.

## Ownership And Synchronization

`PWorld` owns one `IPhysicsScene`. Primitive components own only an `FPhysicsBodyHandle`; the scene owns
the real Jolt shapes and bodies. Components destroy their body while unregistering, before World
destroys the physics scene.

Both World construction paths initialize that runtime service: `PWorld::Initialize` handles a new empty
World, while `FWorldAssetLoader` initializes physics after restoring serialized Level ownership. Serialized
`.pworld` data never stores the backend instance itself; loading reconstructs a fresh Jolt scene for the new
runtime World.

| Body type | Transform authority | Direction |
| --- | --- | --- |
| Static | Component/editor | Component -> Body |
| Kinematic | Gameplay/Movement | Component -> Body |
| Dynamic | Physics | Body -> Component after Step |
| Teleport | Caller | Component -> Body, optionally reset velocity |

Physics writeback uses a guarded SceneComponent path, so a dynamic Body update cannot recursively send
the same Transform back to Jolt. External Transform changes to a dynamic component remain valid
teleports. Shape extent or component-scale changes rebuild the Body so query geometry stays current.

`PCapsuleComponent` supplies the Pawn query shape. `PCubeComponent` supplies Box bodies for the visual
test scene. Static meshes still use simple proxy collision rather than triangle-mesh cooking.

## Queries And Events

The Jolt adapter implements:

- Raycast with ignored-object and sensor filtering;
- Box, Sphere, Capsule, and Point Sweep;
- Overlap with duplicate object removal;
- stable `FObjectHandle` hit identity;
- dynamic gravity and impulse entry points;
- sensor contact conversion into BeginOverlap and EndOverlap;
- blocking contact conversion into component Hit events.

Jolt contact callbacks only collect plain event data. `PWorld` resolves object handles and broadcasts
existing weak-object multicast delegates after Step. Listener exceptions are contained at the World
boundary and cannot abort the physics frame.

`PWorld` also tracks logical active overlap pairs. Duplicate Begin/Persist callbacks are collapsed, and a
raw End callback is checked against the current shapes before gameplay receives it. One continuous Trigger
pass therefore produces one BeginOverlap and one EndOverlap even if the backend refreshes contacts while a
kinematic body moves.

## Visual Verification

Run `PicoSandboxGame` and press `F1`:

1. Confirm `Backend: Jolt 5.6.0` and an increasing physics Step count.
2. Observe the red dynamic cube fall onto the dark floor.
3. Move the Pawn with WASD and confirm Sweep prevents it from crossing the blue walls.
4. Enter and leave the green Trigger volume; Begin/End overlap counters should increase.
5. Press `Raycast Down`; the panel should report a hit object and normalized hit time.
6. Confirm the Pawn body has a stable handle and reports `Kinematic`.

The Sandbox physics geometry is transient runtime content created by `PSandboxGameMode::BeginPlay`; it
does not modify the saved `.pworld` editor scene.

## Automated Verification

`PicoPhysicsTests` covers staged Tick order, scene ownership, body handles, dynamic gravity and
Transform writeback, Raycast, Sweep, Overlap, sensor delegates, stale-handle rejection, body cleanup, and
physics-service reconstruction across serialized World replacement. It also verifies that a continuous
sensor passage emits exactly one logical Begin/End pair.
Existing Movement tests continue to use a fake `IWorldCollisionQuery`, proving Gameplay remains
independent from Jolt.

Two Jolt lifetime requirements are intentionally encoded in the adapter:

- the allocator/factory runtime guard must construct before every Jolt member and destruct after them;
- `JobSystemSingleThreaded` must be initialized with a non-zero job capacity before dynamic simulation.

## Deliberate Limits

At the end of this milestone CharacterMovement, floor-state logic, jumping, step-up, slopes, fixed
physics substeps, asynchronous physics, mesh collision cooking, constraints, and network prediction
were still deferred. Month 8.3 adds the first CharacterMovement layer and bounded fixed substeps;
the remaining features must continue to reuse `MoveComponent`, `IPhysicsScene`, and the staged frame.
