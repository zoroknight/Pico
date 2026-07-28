# Pico Month 03.3: World and Level Lifecycle

## Scope

Month 03.3 introduces the runtime containers that later Actor, Component,
rendering, physics, and networking work can attach to.

The implementation deliberately covers only:

- a reflected `PWorld` runtime context;
- a reflected `PLevel` scene-content container;
- one automatically created persistent level;
- additional level creation, selection, and removal;
- world time and tick counting;
- Engine Loop creation, ticking, and teardown of the active world.

Actor storage, `BeginPlay`, level streaming, rendering scenes, physics scenes,
network drivers, and world serialization remain deferred.

## Object Ownership

The object registry remains the only memory owner. `PWorld` does not store
`std::unique_ptr<PLevel>` values. Instead, it records `FObjectHandle` values,
while the existing `Outer` relationship expresses the logical hierarchy:

```text
GameWorld
└── PersistentLevel
```

The resulting object path is:

```text
GameWorld.PersistentLevel
```

This design reuses the object model from Month 02. Destroying the world tree
destroys its levels first and then destroys the world.

Handles are important because a caller can still destroy a level through the
generic object API. A stale level handle resolves to `nullptr`, and slot reuse
cannot make the old handle point at a replacement object because each slot
receives a new serial number.

## World State

`PWorld` uses the following explicit states:

```text
Uninitialized -> Initialized -> TearingDown -> TornDown
```

`Initialize` creates `PersistentLevel` and selects it as the current level.
Initialization cannot run twice, and a torn-down world cannot be restarted.

`Tick` accepts only finite, non-negative delta time while the world is
initialized. A valid tick increments the world tick count and accumulates world
time. Actor and Component dispatch will be added after those types exist.

`TearDown` is idempotent. It destroys every live level tree, clears all handles,
and leaves the world ready for its own object destruction.

## Level Management

`CreateLevel` creates a `PLevel` with the world as its Outer. Existing object
name rules reject duplicate level names inside one world.

`SetCurrentLevel` accepts only a live level owned by that world. A level from
another world is rejected.

`RemoveLevel` destroys a non-persistent level tree. The persistent level cannot
be removed through this operation. Removing the current level selects the
persistent level again.

If the current level is destroyed externally, `GetCurrentLevel` safely falls
back to the persistent level. `GetLevels` returns only handles that still
resolve to live levels.

## Engine Loop Integration

The startup path is now:

```text
FEngineLoop::PreInit
-> PObjectSystem::Init
-> register PLevel and PWorld
-> NewObject<PWorld>("GameWorld")
-> PWorld::Initialize
-> create GameWorld.PersistentLevel
```

Every engine frame now forwards the measured delta time:

```text
FFrameTimer::Tick
-> PWorld::Tick(DeltaSeconds)
```

Shutdown runs in the opposite direction:

```text
PWorld::TearDown
-> DestroyObjectTree(GameWorld)
-> PObjectSystem::Shutdown
```

`FEngineLoop` stores the active world as an `FObjectHandle`. `GetWorld` resolves
the handle on demand, so engine shutdown cannot leave a dangling world pointer
inside the loop.

## Unreal Engine Comparison

`PWorld` corresponds to a very small subset of `UWorld`: it is the central
runtime context and the future tick entry point.

`PLevel` corresponds to the scene-content role of `ULevel`: it will become the
container used when spawning Actors.

Pico does not yet copy `FWorldContext`, `UGameInstance`, PIE world duplication,
level streaming, World Partition, or the many subsystems owned by `UWorld`.
Those systems should be introduced only when a later milestone has a concrete
need for them.

## Verification

`PicoEngineTests` verifies:

- reflected World and Level class registration;
- persistent level creation and object paths;
- additional level creation, switching, and removal;
- duplicate names and cross-world ownership rejection;
- valid and invalid world ticks;
- idempotent teardown;
- external level destruction and current-level fallback;
- object slot reuse with stale-handle rejection;
- Engine Loop creation, ticking, and destruction of the active world;
- zero-frame and bounded-frame engine runs;
- an empty object registry after shutdown.

## Next Step

Month 03.4 can now add `PActor` and make `PLevel` its owning scene container.
The first Actor lifecycle should cover `SpawnActor`, `DestroyActor`, world/level
lookup, `BeginPlay`, `Tick`, and `EndPlay` without introducing Components yet.
