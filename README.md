# Pico

[English](README.md) | [简体中文](README.zh-CN.md)

Pico is a small, learning-oriented C++ 3D engine inspired by Unreal Engine's architecture. The
project focuses on understanding how engine subsystems connect: startup, objects, reflection,
serialization, worlds, actors, components, transforms, editor tooling, and rendering.

Pico is not intended to compete with production engines. It deliberately keeps each system small
enough to study while preserving clear ownership boundaries and an end-to-end runtime.

![Pico Editor playing the current world in a standalone game window](Docs/Images/PicoEditorStandalonePlay.png)

## Current State

The current implementation can:

- Run a UE-style `PreInit -> Init -> Tick -> Exit` engine loop.
- Launch a standalone `PicoGame` runtime with frame-based input, configurable Action/Axis mappings,
  and a project default map or command-line map override.
- Build a project-specific `PicoSandboxGame` runtime whose statically linked Game Module registers
  native project classes before map loading and creates a project GameInstance afterward.
- Spawn a reflected project Pawn that consumes mapped WASD input through the normal World/Actor Tick.
- Create reflected native objects through `PClass` and `NewObject`.
- Give every registered class a CDO, declare inherited default-subobject templates, and materialize
  independent runtime object graphs through one initialization path.
- Bind type-safe native single-cast and multicast delegates, including generation-safe weak object
  listeners and Actor spawn/destroy lifecycle events.
- Register classes, properties, and functions with thin C++ reflection macros.
- Invoke reflected native functions through `PObject::ProcessEvent` with typed parameters, return
  metadata, inherited lookup, lifecycle checks, and validated future RPC flags.
- Inspect and edit supported properties through generic metadata-driven UI.
- Use PicoInspector as a project-free developer sandbox: invoke reflected functions with generated
  parameter controls and inspect Native Delegate listeners, broadcasts, expiry, and event logs.
- Serialize reflected objects to `.pobj` files and reconstruct them with `PostLoad`.
- Save validated World scene graphs to deterministic `.pworld` files and transactionally reconstruct runtime Worlds without persisting runtime handles.
- Transactionally replace the `FEngineLoop` active World while preserving the old World on load or `PostLoad` failure.
- Treat the editor World as a document with New, Open, Save, and Save As workflows, a stable
  `/Game/...` identity, dirty-state tracking, and save/discard/cancel protection.
- Manage object memory centrally through `FObjectRegistry`, `Outer`, and generation-safe handles.
- Create `PWorld`, `PLevel`, `PActor`, and component instances with explicit lifecycles.
- Use a root scene component as the Actor transform provider.
- Build parent-child scene-component attachment trees with relative and world transforms.
- Attach scene components to named parent sockets, persist socket relationships in `.pworld` v3,
  and load older v1/v2 scenes without socket data.
- Author runtime Camera, Spring Arm, Directional Light, and Point Light components through the
  same reflection, serialization, hierarchy, and editor transaction paths as other components.
- Open a `.pico` project with separate engine and project roots.
- Represent persistent references with validated `/Game/...` asset paths instead of machine-specific
  disk paths.
- Deterministically scan native `.pworld`, `.pmesh`, `.ptex`, and `.pmat` files into a project asset
  registry with case-insensitive lookup and refresh.
- Import triangulated OBJ source data through TinyObjLoader into a validated, deterministic `.pmesh`
  format with positions, normals, UVs, sections, and bounds.
- Import PNG/JPG/TGA/BMP images through stb_image into validated `.ptex` RGBA8 assets, and author
  `.pmat` assets with BaseColor, BaseColorTexture, Metallic, and Roughness.
- Assign reflected Material references to StaticMeshComponents with Undo/Redo and render them through
  an OpenGL 3.3 Cook-Torrance PBR path with renderer-owned texture caching and mipmaps.
- Cache immutable CPU static-mesh data and renderer-owned OpenGL mesh buffers, refreshing them when
  Registry metadata changes.
- Persist `PStaticMeshComponent` asset references and render, pick, highlight, and transform imported
  meshes in the editor viewport.
- Browse project assets by folder, search text, and type in a dockable Content Browser; import OBJ
  files, refresh the Registry, and reimport from project-local source metadata.
- Inspect OBJ geometry before import, choose automatic or explicit source units, preview final mesh
  dimensions, and reopen persisted settings through `Import Options`.
- Batch-delete mixed Static Mesh, Texture, and Material assets with scene/asset dependency
  reporting, optional project-source removal, transaction-backed scene cleanup, and staged rollback.
- Select assets by stable `/Game/...` paths, drag Static Mesh assets into Details, and create or
  transactionally assign mesh components without relying on Registry order.
- Keep reimportable user source files under `Content/Source`; editor-authored native runtime assets
  use type folders such as `Meshes`, `Textures`, `Materials`, and `Maps`.
- Display runtime objects in an Outliner and Details panel.
- Render `PCubeComponent` instances in an interactive OpenGL 3.3 editor viewport.
- Preview the first active scene Camera in the editor, including a Camera attached to the
  `SpringEndpoint` socket of a reflected Spring Arm.
- Visualize non-renderable scene components with selectable editor wireframes: Camera frustums,
  Directional Light arrows, compact Point Light icons with selection-only attenuation spheres,
  and Spring Arm endpoint lines.
- Shade PBR geometry with scene-authored Directional and Point Lights; scenes without authored
  lights retain the legacy default directional light.
- Select Actors and components individually, additively, by range, or with `Ctrl+A`; the viewport
  renders the complete selection set with scene highlights.
- Undo and redo scene hierarchy, Actor Transform, and reflected-property edits through editor
  transactions while restoring the complete multi-selection.
- Copy, paste, and delete multiple Actors in one command, or copy a SceneComponent attachment
  subtree, with remapped scene IDs and one transaction per command.
- Move, rotate, and scale single or multiple scene objects through an ImGuizmo-backed viewport
  gizmo with World/Local coordinates, snapping, primary-object pivots, cancellation, and Undo/Redo.
- Rearrange dockable editor panels and persist each project's layout under `Saved/Editor`.
- Load modern OpenGL entry points through a dedicated GLAD target owned by `PicoRender`.

Debug and Release configurations build successfully, and all nine CTest targets pass.

## Architecture

The primary dependency direction is:

```text
PicoEditor
  -> PicoEditorCore
  -> PicoImGuizmo / PicoImGui
  -> PicoRender
  -> PicoEngine
  -> PicoAsset / PicoObject
  -> PicoCore
```

Supporting tools and samples depend on public runtime interfaces:

```text
PicoReflectionTools -> PicoObject
PicoAssetImport     -> PicoAsset / TinyObjLoader
PicoInspector       -> PicoReflectionTools

PicoSandboxGame
  -> PicoSandboxModule
  -> PicoGameRuntime
  -> PicoEngine / PicoInput / PicoRender
```

| Module | Responsibility |
| --- | --- |
| `PicoCore` | App state, command line, config, logging, names, paths, time, math, project descriptor |
| `PicoInput` | Frame-based key and pointer state plus configurable Action/Axis mappings |
| `PicoAsset` | Validated virtual asset discovery, deterministic project registry, and file metadata |
| `PicoAssetImport` | Developer-only OBJ conversion into validated native static-mesh assets |
| `PicoObject` | `PObject`, `PClass`, `PProperty`, `PFunction`, ProcessEvent, delegates, registry, handles, Outer graph, serialization |
| `PicoEngine` | Engine loop, World, Level, Actor, components, attachment, primitive scene data |
| `PicoRender` | GLAD-backed OpenGL, shaders, geometry, framebuffer, scene traversal and draw submission |
| `PicoGameRuntime` | Reusable project launch, GLFW window, input polling, frame loop, and runtime rendering |
| `PicoEditorCore` | UI-independent World documents, object/asset selection, asset operations, commands, clipboard, transactions, and transforms |
| `PicoEditor` | World file dialogs, Content Browser, asset workflow controller, Outliner, Details, editor camera, viewport picking, tool state, and transform gizmo UI |
| `PicoReflectionTools` | Generic metadata inspection and reflected-property helpers |
| `PicoSandboxModule` | Project classes, Game Module startup, GameInstance creation, reflection, serialization, and tests |

The runtime modules do not depend on ImGui. `PicoCore`, `PicoAsset`, `PicoObject`, and `PicoEngine` also remain
independent of GLFW and OpenGL.

## Runtime Object Model

Pico keeps four relationships separate:

```text
PClass       = what type an object is
Outer        = which object owns its name and lifetime
Handle       = how the registry safely resolves it later
AttachParent = which scene component provides its transform space
```

The current scene hierarchy is:

```text
PWorld
  -> PLevel
    -> PActor
      -> PActorComponent
        -> PSceneComponent
          -> PCameraComponent
          -> PSpringArmComponent
          -> PLightComponent
            -> PDirectionalLightComponent
            -> PPointLightComponent
          -> PPrimitiveComponent
            -> PCubeComponent
            -> PStaticMeshComponent
```

`FObjectRegistry` owns object memory. Handles do not extend lifetime, and stale handles resolve to
`nullptr`. Outer defines naming and deterministic destruction order. This is the foundation for a
future tracing garbage collector, not a tracing GC implementation itself.

## Project Boundary

Pico separates engine installation data from user-authored project data:

```text
EngineRoot/
  Source/
  ThirdParty/
  Config/

ProjectRoot/
  MyGame.pico
  Source/
  Content/
  Config/
  Intermediate/
  Saved/
```

Automatic editor and tool writes are limited to project `Content`, `Intermediate`, and `Saved`.
Engine source and project source are not automatic write targets.

`Projects/PicoSandbox/PicoSandbox.pico` is the maintained example project. The same project shape
can live outside the Pico repository.

Native project assets use virtual identifiers such as `/Game/Maps/EditorWorld.pworld`. The registry
maps those identifiers to files under the active project's `Content` directory; persistent scene and
object data never stores an absolute workstation path.

## Requirements

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 with the **Desktop development with C++** workload
- MSVC v143 and a Windows 10/11 SDK
- CMake 3.22 or newer
- Git for Windows
- A GPU and driver supporting OpenGL 3.3

GLFW, Dear ImGui, ImGuizmo, TinyObjLoader, and stb_image are included under `ThirdParty`.

## Quick Start

```powershell
git clone https://github.com/zoroknight/Pico.git
Set-Location Pico
powershell -ExecutionPolicy Bypass -File .\Scripts\SetupWindows.ps1
```

The setup script checks the environment, generates a Visual Studio 2022 x64 build, compiles every
target, and runs the tests. Build products are written to `Build`.

Start the editor:

```powershell
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

The editor starts maximized. Its default UI scale is `1.4`; override it when needed:

```powershell
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico -uiscale=1.6
```

Start the game runtime without editor UI:

```powershell
.\Build\Debug\PicoSandboxGame.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

The project runtime reads `[Game] DefaultMap` and the Action/Axis mappings in `[Input]` from
`Config/Pico.ini`. `[Game] Executable` selects the project program used by editor Play.
`-map=/Game/Maps/Example.pworld` overrides the default map, and `-frames=N` supports automated
smoke runs. The generic `PicoGame` target remains available for projects without native code.

The editor toolbar's green triangle saves the current World when needed and launches this standalone
runtime with the current document's `/Game/...` map path. While it is running, the control becomes
a red square that stops the game process. Tooltips identify both controls; closing either process is
detected and the editor returns to its ready state.

## Editor Controls

The editor starts with an empty scene:

```text
GameWorld
  -> PersistentLevel
```

- Use `Add > Empty Actor` to create an editor-authored Actor with `DefaultSceneRoot`.
- Use `Add > Cube` to create an Actor whose renderable `PCubeComponent` is also its root.
- Use `Add > Camera`, `Spring Arm`, `Directional Light`, or `Point Light` to create reflected,
  transaction-backed scene actors. The same types are available under `Add Component`.
- To build a camera rig, select a Spring Arm component and add a Camera component. Pico attaches
  it to the named `SpringEndpoint` socket automatically. Edit `TargetArmLength`, `SocketOffset`,
  and `TargetOffset` in Details.
- Enable `Scene Camera` in the toolbar to preview the first active Camera component. Disable it to
  return to the independent editor fly camera.
- Edit a Light component's `bEnabled`, `LightColor`, and `Intensity`; Point Lights additionally
  expose `AttenuationRadius`. The renderer currently uses one Directional Light and up to four
  Point Lights.

Camera-rig parameter reference:

| Component | Property | Meaning |
| --- | --- | --- |
| Camera | `bActive` | Makes the component eligible for `Scene Camera`; the first active Camera is used. |
| Camera | `VerticalFieldOfViewDegrees` | Vertical field of view in degrees; larger values show a wider view. |
| Camera | `NearPlane` / `FarPlane` | Nearest and farthest rendered distances. |
| Spring Arm | `TargetArmLength` | Distance from the arm origin to `SpringEndpoint` along local `-X`. |
| Spring Arm | `TargetOffset` | World-space offset applied to the arm origin. |
| Spring Arm | `SocketOffset` | Arm-local offset applied at `SpringEndpoint`, useful for over-shoulder cameras. |

An attached Camera normally keeps an identity Relative Transform and lets the Spring Arm control
distance, rotation, and offset. Collision retraction and camera lag are not implemented yet.
- Select a `.pmesh` in Content Browser, then use `Add > Static Mesh` or double-click the asset to
  create an Actor. `Ctrl` toggles assets, `Shift` selects a range, and `Ctrl+A` selects every asset
  visible under the current folder, search, and type filters.
- Use `Import OBJ` to inspect source geometry, choose Auto/Centimeters/Meters/Millimeters/Normalize/
  Custom scaling, copy the source into `Content/Source/Meshes`, and build a native `.pmesh` under
  `Content/Meshes`.
- Use `Import Texture` to create a native `.ptex`, then `Create Material` to edit BaseColor,
  BaseColorTexture, Metallic, and Roughness. Double-click a `.pmat` to edit it later.
- With Content Browser focused, press `Delete` or use its Delete button to review scene and asset
  references and remove selected Static Mesh, Texture, and Material assets as one batch.
  Scene-focused `Delete` still removes objects.
  `Reimport` uses the saved settings; `Import Options` edits them before rebuilding.
- Use `Delete` to review references before removing assets. Project-local source deletion is
  optional; scene cleanup is one Undo/Redo transaction, Material-to-Texture references are updated
  transactionally with staged files, and file deletion itself is not a World transaction.
- Drag a Static Mesh row onto the Details `AssetPath` value, or use `Use Selected` and `Clear`.
  Assignment and clearing participate in scene Undo/Redo; import and reimport do not.
- Add Scene or Cube components to a selected Actor, or add them as children of a selected scene
  component.
- Press `F2` to rename a selected Actor or Component and `Delete` to remove it. Deleting a scene
  component removes its complete attachment subtree.
- Right-click World, Level, Actor, or Component nodes for context-sensitive creation, rename, root,
  and delete commands.
- Left-click rendered geometry to select its owning Actor. Selecting an Actor outlines all of its
  visible CubeComponents; selecting a component in the Outliner outlines only that component.
- Hold `Ctrl` while clicking to toggle objects in the selection. Hold `Shift` in the Outliner to
  select a range, or use `Ctrl+A` to select all scene Actors.
- Press `Q` for selection, `W` for translation, `E` for rotation, and `R` for scale. The toolbar
  exposes the same transform modes.
- Press `F` to frame selected Actors or components from their transformed world bounds. Camera
  distance, near plane, and movement speed adapt to very small and very large meshes.
- Use `World` coordinates to align the gizmo with the scene axes, or `Local` coordinates to align
  it with the primary selected object's rotation. Multi-object rotation and scaling use the primary
  object as their pivot.
- Enable `Snap` to quantize translation, rotation, and scale. Press `Esc` during a drag to restore
  every target to its pre-drag transform.
- An Actor gizmo uses its root component as the Actor pivot. If visible geometry is offset beneath
  a `DefaultSceneRoot`, select the child component in the Outliner to transform it around the
  geometry's own origin.
- Use `Ctrl+Z` to undo and `Ctrl+Y` or `Ctrl+Shift+Z` to redo scene hierarchy, Actor Transform, and
  reflected-property edits. One continuous value drag creates one transaction.
- Use `Ctrl+C` and `Ctrl+V` to copy and paste an Actor with all of its components, or a selected
  SceneComponent with its complete attachment subtree. Paste is one undoable transaction.
- Edit Actor transforms in the Details panel to move, rotate, and scale the cube.
- Edit `CubeComponent` reflected properties to change relative transform, extent, color, or visibility.
- Hold the right mouse button to capture the cursor and move the mouse to look around.
- While holding the right mouse button, use `W/A/S/D` to fly and `Q/E` to descend or ascend.
- Hold `Shift` to move four times faster; use the mouse wheel while flying to adjust camera speed.
- Use the toolbar to add scene children, choose a root, or destroy runtime objects.
- Drag panel tabs to rearrange or tab the workspace; use `Reset Layout` to restore the default.

Use `Ctrl+N` to create an untitled World, `Ctrl+O` to choose a `.pworld` under project Content,
`Ctrl+S` to save the current document, and `Ctrl+Shift+S` to choose a new file. World assets can
also be opened from the Content Browser. The window title marks dirty documents with `*`, and
New, Open, and Exit offer save/discard/cancel protection. Failed loads preserve the current World
and document identity; safe saves use temporary replacement and retain a `.bak` of overwritten files.
Saving scene data never rewrites C++ source.

Editor panel layout is separate from scene data and persists in
`Projects/<ProjectName>/Saved/Editor/PicoEditorLayout.ini`.

## Programs and Samples

| Target | Purpose |
| --- | --- |
| `PicoLaunch` | Headless engine-loop and World lifecycle executable |
| `PicoEditor` | Runtime scene editor with OpenGL viewport |
| `PicoGameRuntime` | Reusable GLFW/input/render loop linked by game targets |
| `PicoGame` | Generic standalone runtime without project-native code |
| `PicoSandboxGame` | Project runtime with Sandbox module, GameInstance, and controllable Pawn |
| `PicoInspector` | Project-free developer sandbox for reflected objects, functions, and subsystem experiments |
| `PicoReflectionDemo` | Console reflection walkthrough |
| `PicoAssetTool` | Developer command-line OBJ to `.pmesh` importer |
| `PicoSandboxDemo` | Complete project-side create, edit, save, destroy and load workflow |

Example commands:

```powershell
.\Build\Debug\PicoLaunch.exe -frames=5
.\Build\Debug\PicoReflectionDemo.exe
.\Build\Debug\PicoAssetTool.exe import-obj source.obj destination.pmesh
.\Build\Debug\PicoInspector.exe
.\Build\Projects\PicoSandbox\Debug\PicoSandboxDemo.exe
.\Build\Debug\PicoSandboxGame.exe .\Projects\PicoSandbox\PicoSandbox.pico
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

PicoInspector starts in its Native Delegate experiment. Use `Runtime Browser -> Functions` for
generic `ProcessEvent` calls, or `Experiments` to inspect listener lifetime and broadcast logs.
See the [visual verification guide](Docs/PicoInspector_VisualVerificationGuide.zh-CN.md) for the
test sequence and the purpose of every step. Override its default readable scale with
`-uiscale=1.4` when needed.

## Build and Test

Generate and build manually:

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Debug --parallel
ctest --test-dir Build -C Debug --output-on-failure
```

Build and test Release:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\SetupWindows.ps1 -Configuration Release
```

Open Pico in Visual Studio:

```text
Scripts\OpenFolder.bat
Scripts\GenerateProjectFiles.bat
Scripts\OpenSolution.bat
```

The generated solution is `Build\Pico.sln`.

## Repository Layout

```text
Pico/
  Config/                  Engine configuration
  Docs/                    Milestone and authoring documentation
  Projects/PicoSandbox/    Maintained external-style sample project
  Scripts/                 Windows setup and build scripts
  Source/
    Developer/             Development-only reflection tools
    Editor/                PicoEditor and PicoInspector
    Runtime/               Core, Object, Engine, Render and Launch
    Samples/               Reusable engine-side samples
  Tests/                   Core, Object, Engine and Sandbox tests
  ThirdParty/              GLAD, GLFW, Dear ImGui and ImGuizmo
```

## Reflection Authoring

Pico currently uses thin native C++ macros:

```cpp
class PExample final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PExample, Pico::PObject)

private:
    Pico::int32 Health = 100;
};
```

The `.cpp` explicitly defines the class and reflected properties:

```cpp
PICO_DEFINE_CLASS(PExample)

bool PExample::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Health);
    return Class.AddProperties(std::move(Properties));
}
```

There is no UHT-like header tool yet. A future PicoHeaderTool may generate this boilerplate while
continuing to use the same runtime metadata system.

See:

- [Remaining Development Roadmap (Chinese)](Docs/Pico_Remaining_Development_Roadmap.zh-CN.md)
- [Reflection Authoring Guide](Docs/ReflectionAuthoringGuide.md)
- [Native Delegate Authoring Guide (Chinese)](Docs/DelegateAuthoringGuide.md)
- [PicoInspector Developer Sandbox Plan (Chinese)](Docs/PicoInspector_DeveloperSandbox_Plan.zh-CN.md)
- [PicoInspector Visual Verification Guide (Chinese)](Docs/PicoInspector_VisualVerificationGuide.zh-CN.md)
- [PicoSandbox Guide](Projects/PicoSandbox/README.md)
- [Month 3 Editor Viewport](Docs/Month03_10_Editor3DViewport.md)
- [Month 3 Editor Docking](Docs/Month03_11_EditorDocking.md)
- [Month 3 GLAD Integration](Docs/Month03_12_GLADIntegration.md)
- [Month 4 Editor Transactions](Docs/Month04_9_EditorTransactions.md)
- [Month 4 Property Transactions](Docs/Month04_10_EditorPropertyTransactions.md)
- [Month 4 Editor Clipboard](Docs/Month04_11_EditorClipboard.md)
- [Project Game Module and Runtime Target](Docs/Month06_2_ProjectGameModule.md)
- [Class Default Objects and Unified Construction](Docs/Month03_13_ClassDefaultObjects.md)
- [Default Subobject Templates](Docs/Month03_14_DefaultSubobjects.md)
- [Native Delegates and Weak Object Binding](Docs/Month03_15_NativeDelegates.md)
- [Reflected Functions and ProcessEvent](Docs/Month03_16_ReflectedFunctions.md)

## Roadmap

The asset-driven editor, imported static meshes, materials, textures, PBR rendering, standalone Play,
and the first project Game Module/GameInstance path are complete. The remaining learning path is:

- PicoHeaderTool and tracing garbage collection
- A UE-inspired Gameplay Framework with GameMode, GameState, PlayerController, PlayerState, Pawn,
  Character, and movement components
- Jolt physics, character movement, and a small animation integration
- Replication, RPC, transform synchronization, client prediction, and correction
- Dedicated-server/WAN validation, Cook, Package, and a standalone Windows build
- A compact Gameplay Ability System with AbilityTask, followed by AI tools and an Agent workflow

The maintained schedule and acceptance criteria are in the
[Remaining Development Roadmap](Docs/Pico_Remaining_Development_Roadmap.zh-CN.md). Detailed milestone
notes are also available in [`Docs`](Docs).
