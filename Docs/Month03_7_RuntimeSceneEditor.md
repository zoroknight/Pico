# Month 03.7 Runtime Scene Editor

This milestone adds `PicoEditor`, a runtime scene editor that uses the Engine module without making
the Engine depend on editor code.

## Module Direction

```text
PicoEditor
  -> PicoEngine
  -> PicoReflectionTools

PicoEngine
  -> PicoObject
  -> PicoCore
```

`PicoEditor` also uses `PicoImGui` for its native desktop interface. The existing `PicoInspector`
remains a generic reflection/object tool. The earlier standalone `PicoSandboxEditor` teaching
sample was later retired in favor of opening `PicoSandbox.pico` through this shared editor.

## Runtime Session

The editor owns an `FEngineLoop`. Engine initialization creates `GameWorld` and its persistent
level. The editor then creates a small scene:

```text
GameWorld
  -> PersistentLevel
    -> CubeActor
      -> RootComponent [Root]
```

Each editor frame ticks the same `PWorld` that the scene panels inspect.

Month 03.9 extends the Outliner to display the real `PSceneComponent` attachment hierarchy and
allows child scene components to be created from the toolbar.

## Persistence Boundary

Editor actions only mutate live objects in the current `FObjectRegistry` session. Spawning,
transform editing, root selection, and destruction do not rewrite C++ headers, source files, CMake
files, or sample code. Closing the editor discards those runtime scene changes. The editor is
launched with a `.pico` descriptor, so its ProjectRoot is independent from EngineRoot.

Future scene persistence will write explicit content/scene assets. Source generation, if Pico later
adds a UHT-like authoring tool, remains a separate command that must be invoked explicitly.

## Scene Outliner

The tree is built through runtime ownership and membership APIs:

```text
PWorld::GetLevels
  -> PLevel::GetActors
    -> PActor::GetComponents
```

Selection is stored as `FObjectHandle`, not as a persistent raw pointer. Destroyed objects therefore
disappear safely when their handles stop resolving.

## Details

Selecting an Actor edits its world transform through:

```text
PActor::SetActorTransform
  -> RootComponent
    -> PSceneComponent::SetWorldTransform
```

Selecting a scene component shows `RelativeTransform` through its reflected `PProperty`. Editing
either view changes the same transform stored by the root scene component.

The editor also supports spawning a plain Actor with a scene root, adding a missing scene root,
selecting an owned scene component as root, and destroying Actors or components.

## Deferred Work

- OpenGL scene viewport and camera
- Primitive and static-mesh components
- World/Actor/component graph serialization
- Undo/redo and editor transactions
