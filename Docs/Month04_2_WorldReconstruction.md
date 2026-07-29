# Month 04.2 World Reconstruction

This milestone converts validated `FWorldAssetData` into a complete runtime
World. Loading is transactional: failure destroys every object created by the
attempt and returns null.

## Reconstruction Flow

`CreateWorldFromAssetData` performs these phases:

```text
Validate records
  -> create World
  -> create Levels
  -> select persistent and current Levels
  -> create Actors
  -> create Components
  -> apply reflected properties
  -> restore Actor roots
  -> restore SceneComponent attachments
  -> call PostLoad
```

Object records may appear in any order. The loader groups creation by runtime
type and maintains a temporary `FSceneObjectId -> PObject*` map. The map is
discarded after references have been converted into new runtime handles.

## Runtime Ownership

The loader is a narrow friend of the runtime object types. It populates the
same private handle arrays used by normal World, Level, and Actor APIs without
exposing load-only mutation to gameplay code.

`OuterId` selects the owning runtime object during construction:

```text
World
  -> Level
    -> Actor
      -> ActorComponent
```

Relationship records are applied only after every object exists:

- `SetRootComponent` rebuilds an Actor root handle
- `AttachToComponent` rebuilds parent and child attachment handles

Reflected properties are applied before relationships, while `PostLoad` runs
after the complete object graph is available.

## Failure Semantics

Reconstruction reports dedicated errors for:

- object creation failure
- reflected property type mismatch
- reflected property access failure
- relationship restoration failure
- `PostLoad` failure

Any failure calls `DestroyObjectTree` on the temporary World. No partially
loaded object remains in `FObjectRegistry`.

## Guarantees

Tests verify that reconstruction preserves:

- persistent and current Level selection
- Actor roots and component attachments
- reflected component properties and transforms
- deterministic capture bytes after a load/capture round trip
- one `PostLoad` call per loaded object

Reconstructed objects receive fresh `FObjectHandle` values. Scene IDs are used
only while reconstructing references and never replace runtime handles.

## Deferred Work

The following remain separate milestones:

- replacement of the EngineLoop and editor active World
- temporary root naming and final rename during active-World replacement
- editor Save/Open commands
- persistent editor GUIDs
- Undo/Redo and clipboard operations

Atomic `.pworld` file persistence is implemented by
`Month04_3_WorldFilePersistence.md`.
