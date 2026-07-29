# Month 04.4 Engine World Replacement

This milestone lets `FEngineLoop` transactionally replace its active World
from a `.pworld` file without sacrificing the current World on failure.

## Pure File Loading

`LoadWorldAssetDataFromFile` separates file parsing from object construction:

```text
.pworld
  -> bounded file read
  -> archive parsing
  -> graph validation
  -> trailing-data check
  -> FWorldAssetData
```

The output data changes only after the complete file succeeds. The existing
`LoadWorldFromFile` composes this function with `CreateWorldFromAssetData`.

## Controlled Rename

`RenameObject` validates that the object is live, not being destroyed, has a
non-None destination name, and has no same-Outer name conflict. Handles,
Outers, and object addresses remain unchanged.

For a same-name replacement, the old root World is temporarily renamed to an
unused `__PicoPreviousWorld_N` name. This frees the serialized World name while
keeping the old object tree alive.

## Transaction

`FEngineLoop::LoadWorld` performs:

```text
read and validate pure data
  -> reject unrelated top-level name conflicts
  -> temporarily rename the old World when needed
  -> reconstruct the complete new World
  -> on failure, restore the old World name
  -> on success, commit the new WorldHandle
  -> tear down and destroy the old World
```

Loading is rejected while the EngineLoop is uninitialized or ticking its
World. The next frame resolves and ticks the newly committed handle.

## Guarantees

Tests verify:

- same-name World replacement
- reflected properties and attachment restoration
- old handle invalidation after commit
- old name, handle, and object count restoration after `PostLoad` failure
- malformed files leave the active World untouched
- the next EngineLoop frame ticks the new World

## Deferred Work

`FPicoEditorApp` still owns a separate World handle and selection handle.
Editor Save/Open commands must update those handles, clear stale selection,
and refresh editor-facing state after an EngineLoop replacement.
