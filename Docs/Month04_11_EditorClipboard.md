# Month 04.11 Editor Clipboard

This milestone adds an editor-local scene clipboard and undoable `Ctrl+C` / `Ctrl+V` commands.

## Clipboard Boundary

`FEditorSceneClipboard` lives in `PicoEditorCore`, independently of ImGui. Copy captures a validated
World and extracts one of two record sets:

```text
Actor
  -> Actor record
  -> every component owned by that Actor
  -> root and internal attachment relations

SceneComponent
  -> selected component record
  -> every attachment descendant
  -> internal attachment relations
```

The clipboard stores reflected property records by value. It is process-local editor state and is
not written to `.pworld`, the operating-system clipboard, or the Undo history.

## ID Remapping

Clipboard IDs identify relationships only inside the copied record set. Paste allocates a fresh ID
for every record after the greatest ID in the destination World:

```text
old Actor 10       -> new Actor 31
old Root 11        -> new Root 32
old Child 12       -> new Child 33
```

The same map rewrites:

- `FSceneObjectRecord::Id`
- `FSceneObjectRecord::OuterId`
- `FSceneRelationRecord::ObjectId`
- `FSceneRelationRecord::RootComponentId`
- `FSceneRelationRecord::AttachParentId`

Actor paste changes the copied Actor's Outer to the current Level. Component paste changes every
copied component's Outer to the destination Actor. Internal component attachments are preserved.
The copied subtree root attaches to the selected SceneComponent, attaches to the selected Actor's
root, or becomes the root when that Actor has no root.

## Names and Reconstruction

Names are unique within an Outer. A collision produces:

```text
Cube
Cube_Copy
Cube_Copy_2
```

After merging records and relations, the clipboard validates the complete `FWorldAssetData`.
`PicoEditor` then begins one transaction, calls `FEngineLoop::ReplaceWorld`, resolves the pasted
object by its reconstructed path, selects it, and commits. Undo and Redo therefore remove or restore
the complete pasted hierarchy in one step.

Outliner context-menu paste is deferred until tree drawing finishes. Replacing the World while an
Outliner node is still rendering would otherwise leave the current ImGui call stack holding
pointers into the destroyed World.

## Commands

```text
Ctrl+C  Copy selected Actor or SceneComponent subtree
Ctrl+V  Paste at the current destination
```

Actor content always pastes into the current Level. SceneComponent content requires an Actor or
SceneComponent selection. The Edit menu and matching Outliner context menus expose the same shared
commands.

## Test Coverage

`PicoEditorTests` verifies:

- Actor component records, root relation, attachments, transforms, and reflected properties;
- fresh IDs and `_Copy` / `_Copy_2` collision handling;
- whole-Actor Undo and Redo;
- component-subtree attachment beneath an existing component;
- component paste becoming the root of an empty Actor;
- complete object release after reconstructed Worlds are destroyed.

## Deferred Work

Cut, multi-selection, cross-process clipboard transfer, cross-project asset copying, and reflected
object-reference remapping remain outside this milestone. Pico's reflected property types currently
contain values rather than object references, so no external reflected references need repair yet.
