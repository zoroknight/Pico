# Month 04.9 Editor Transactions

This milestone adds the first Undo/Redo boundary around discrete PicoEditor scene operations.
Transactions are editor-only state and are never serialized into `.pworld`.

## Transaction Manager

`PicoEditorCore` owns `FEditorTransactionManager`, independently of ImGui and the PicoEditor
executable. A transaction follows:

```text
Begin
  -> capture Before FWorldAssetData and selected object path
perform one editor action
Commit
  -> capture After FWorldAssetData and selected object path
  -> push Undo stack
  -> clear Redo stack
```

Failed actions call `Cancel` and do not create history. The default history limit is 64 entries;
the oldest entry is discarded when the limit is reached.

## Snapshot Restoration

Undo restores the Before snapshot and moves the entry to Redo. Redo restores After and moves the
entry back to Undo. `FEngineLoop::ReplaceWorld` provides the same transactional replacement
guarantee used by file loading:

```text
validate data
  -> build and PostLoad a temporary World
  -> commit the new active World
  -> tear down and destroy the old World
```

If reconstruction fails, the current World and transaction stacks remain unchanged.

Runtime `FObjectHandle` values are intentionally not preserved because restoration constructs a new
World. The editor stores the selected object's path in each snapshot, resolves that path after a
successful replacement, and falls back to the active World if the object is unavailable.

## Covered Actions

The following discrete operations create one transaction each:

- create Empty Actor or Cube Actor;
- add a default root, SceneComponent, or CubeComponent;
- promote a SceneComponent to root;
- delete an Actor or complete component attachment subtree;
- rename an Actor or Component.

Keyboard commands are:

```text
Ctrl+Z       Undo
Ctrl+Y       Redo
Ctrl+Shift+Z Redo
```

The Edit menu exposes the same commands and shows the next transaction description. Successfully
opening a World from disk clears both stacks so history from the previous World cannot be applied.

## UE5 Comparison

UE5's transaction system records participating UObject state through per-object transaction
records and `Serialize`, rather than rebuilding the complete World for every Undo. Pico starts with
whole-World Before/After snapshots because its scenes are currently small and its reconstruction
path is already validated.

The public transaction boundary is deliberately separate from the snapshot backend. Pico can later
replace full snapshots with object-local records without changing editor actions, shortcuts, or
future AI command entry points.

## Deferred Work

Transform and reflected-property widgets are not transactional yet. They require an interaction
boundary so a continuous drag becomes one history entry:

```text
item activated              -> Begin
continuous value changes    -> mutate current object
item deactivated after edit -> Commit
```

Clipboard operations, multi-selection, and persisted transaction history also remain outside this
milestone.
