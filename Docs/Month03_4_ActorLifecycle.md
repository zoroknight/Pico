# Month 03.4 Actor Lifecycle

This milestone adds the first gameplay runtime object: `PActor`.

## Goals

- Introduce a reflected `PActor` base class.
- Spawn actors through `PWorld`, not through gameplay code calling `NewObject` directly.
- Store level membership in `PLevel` while keeping memory ownership in the object registry.
- Dispatch `BeginPlay`, `Tick`, and `EndPlay` from `PWorld`.
- Support actor destruction during tick without invalidating the active tick traversal.

## Runtime Shape

- `PWorld` is a `PObject` instance and owns the high-level runtime state.
- `PLevel` is a `PObject` instance whose Outer is its `PWorld`.
- `PActor` is a `PObject` instance whose Outer is its `PLevel`.
- The object registry owns memory for all of them.
- `PLevel` stores actor membership as `FObjectHandle` values, so stale or externally destroyed actors are filtered out when queried.

Example path:

```text
GameWorld.PersistentLevel.Hero
```

This means:

- `GameWorld` is the world object.
- `PersistentLevel` is a level object outered to the world.
- `Hero` is an actor object outered to the level.

## Lifecycle

Actor lifecycle is deterministic for now:

1. `PWorld::SpawnActor` validates that the class derives from `PActor`.
2. `NewObject` creates the actor and transfers memory ownership to the object registry.
3. `PLevel` records the actor handle as level membership.
4. The first valid `PWorld::Tick` starts play and dispatches `BeginPlay`.
5. Each valid world tick dispatches `Tick` to live, begun, non-pending actors.
6. `PWorld::DestroyActor` marks the actor pending destroy and dispatches `EndPlay`.
7. If destruction happens during actor ticking, the final registry destroy is deferred until the end of that world tick.
8. `PWorld::TearDown` dispatches `EndPlay` to live actors, then destroys level object trees.

## What This Does Not Do Yet

- No components.
- No transform.
- No actor attachment hierarchy.
- No ticking groups or tick prerequisites.
- No garbage collection reachability pass.
- No actor serialization or replication.

Those systems should build on top of this layer instead of replacing it.
