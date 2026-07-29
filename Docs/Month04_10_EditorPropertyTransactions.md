# Month 04.10 Editor Property Transactions

This milestone extends the editor transaction boundary to continuous Details-panel edits.

## Interaction Boundary

ImGui controls edit local value copies before Pico writes them back to the selected object. The
editor uses that separation to capture the World before the first mutation:

```text
control activated
  -> begin transaction and capture Before
value changes while active
  -> write the current value to the object
control deactivated
  -> capture After and commit once
```

Activating a control without changing its value cancels the pending transaction. Switching
selection, starting another editor command, saving, or opening a World finishes the active edit
first. If the edited control disappears without a normal deactivation event, the end-of-frame
fallback also finishes it.

## Covered Values

Actor Transform editing and every reflected type currently exposed by the Details panel use the
same interaction boundary:

- `int32`
- `float`
- `bool`
- `FVector3`
- `FRotator`
- `FTransform`

One drag may write many intermediate values into the live World, but it creates one Undo entry with
only the initial and final World snapshots. Undo and Redo therefore never stop at intermediate drag
positions.

## Persistence

Transactions remain editor-only memory state. `Ctrl+S` first finishes an active property edit and
then persists the resulting World to `Content/Maps/EditorWorld.pworld`. The Undo and Redo stacks are
not serialized.

## Test Coverage

`PicoEditorTests` verifies that:

- multiple Transform updates between Begin and Commit produce one transaction;
- Undo restores the original Actor Transform;
- Redo restores the final Transform rather than an intermediate value;
- a reflected `RelativeTransform` value survives snapshot reconstruction in both directions.

## Deferred Work

Viewport Transform gizmos can reuse this interaction boundary when they are introduced. Clipboard
operations, multi-selection, object-local transaction records, and persisted transaction history
remain outside this milestone.
