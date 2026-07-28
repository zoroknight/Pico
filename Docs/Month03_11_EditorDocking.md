# Month 03.11 Editor Docking

This milestone replaces PicoEditor's fixed three-column table with a persistent docking workspace.
It changes editor layout management without changing runtime scene behavior.

## Docking Workspace

The editor creates one full-window ImGui DockSpace and three independent tool windows:

```text
Scene Outliner | Viewport | Details
```

Each tool window can be resized, moved, tabbed with another window, or docked on a different edge.
The first launch builds the layout above. `Reset Layout` restores it after the user rearranges the
workspace.

The existing panel functions still own their original responsibilities:

- Scene Outliner traverses and selects runtime objects.
- Viewport displays the OpenGL framebuffer and owns editor-camera input.
- Details edits Actor, Component, Transform, and reflected-property state.

Docking therefore does not introduce a second scene model or duplicate editor state.

## Project-Local Persistence

After `FEngineLoop::PreInit` establishes the active project, PicoEditor asks `FPaths` for:

```text
<Project>/Saved/Editor/PicoEditorLayout.ini
```

ImGui loads and saves window positions, sizes, tabs, and Dock node relationships there. The backing
path string remains alive until ImGui shuts down, and the layout is saved explicitly at exit.

If no valid project write path exists, ini persistence is disabled. PicoEditor never falls back to
writing `imgui.ini` beside the engine source.

## Important Boundary

Layout persistence and scene persistence are separate:

- Moving editor panels updates `Saved/Editor/PicoEditorLayout.ini`.
- Editing an Actor or Component still changes runtime memory only.
- No editor operation rewrites engine or project C++ source.

Scene graph serialization and project scene assets remain future work.
