# Month 04.5 Editor World Persistence

This milestone makes `PicoEditor` the single visual editor entry point for `PicoSandbox`.

## Ownership

`FEngineLoop` is the sole source of the active `PWorld`. `FPicoEditorApp` stores an engine-loop
pointer instead of caching a second World handle. The Outliner, Details panel, and viewport resolve
the current World through `FEngineLoop::GetWorld()` every frame.

## Project Map

The editor uses the active project's default map path:

```text
Content/Maps/EditorWorld.pworld
```

`Ctrl+S` captures and atomically saves the active World. `Ctrl+O` calls the transactional
`FEngineLoop::LoadWorld` path. A failed load preserves the active World and selection. A successful
load selects the replacement World; the editor panels and viewport observe it on the same frame.

## Editor Boundary

The standalone `PicoSandboxEditor` reflection teaching program has been removed. Project classes,
assets, console demonstrations, and tests remain in `Projects/PicoSandbox`; reusable scene editing
stays in `Source/Editor/PicoEditor`.
