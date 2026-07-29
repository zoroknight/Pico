# Month 04.7 Scene Hierarchy Editing

This milestone completes the direct scene-hierarchy actions that will later be wrapped by editor
transactions.

## Creation Rules

The editor offers two Actor templates:

```text
Empty Actor
  -> DefaultSceneRoot [Root]

Cube
  -> CubeComponent [Root]
```

Selecting an Actor and adding a Scene or Cube component attaches the new component to its current
root. Selecting a SceneComponent attaches the new component directly below that selection. This
supports arbitrary same-Actor hierarchies such as:

```text
Actor_1
  -> DefaultSceneRoot [Root]
       -> CubeComponent_1
            -> CubeComponent_2
```

Names are generated per component kind and retried when a loaded map already contains the same
name.

## Rename

`F2` and the Outliner context menu open the same rename action. Actor and Component names must
start with a letter or underscore and may contain letters, numbers, and underscores. The Object
Registry enforces uniqueness within the same Outer. Renaming preserves the runtime handle,
component ownership, attachment relations, and serialized references.

## Delete

The toolbar, `Delete` key, and Outliner context menu call the same deletion action.

`PActor::DestroyComponent` owns component deletion policy. A SceneComponent is collected in
post-order with all attachment descendants, then every component is destroyed and removed from the
Actor component list. Deleting the root clears the Actor root handle. Deleting an Actor continues
to destroy its complete Outer tree.

The first editor rule is deliberately simple:

```text
delete SceneComponent -> delete its complete attachment subtree
delete root subtree   -> keep Actor without a root
delete Actor          -> delete Actor and all Components
```

Reparenting children while preserving world transforms can be added later as a separate explicit
command.

## Context Menus

World and Level nodes expose Actor creation. Actor nodes expose component creation, rename, and
delete. Component nodes expose child creation, root promotion, rename, and delete. Context menus
only dispatch shared editor actions; they do not duplicate object mutation logic.

## Transaction Boundary

These actions are intentionally centralized before Undo/Redo work begins. The next transaction
milestone can wrap Actor creation, component creation, rename, root promotion, and subtree deletion
without changing toolbar, keyboard, context-menu, or future AI command entry points.
