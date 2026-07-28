# Month 03.10 Editor 3D Viewport

This milestone connects Pico's runtime scene graph to a visible OpenGL editor viewport.

## Renderable Scene Components

Rendering starts from scene data rather than an editor-only preview:

```text
PSceneComponent
  -> PPrimitiveComponent
    -> PCubeComponent
```

`PPrimitiveComponent` adds visibility and color. `PCubeComponent` adds a half extent. These classes
remain free of OpenGL types and live in `PicoEngine`.

The sample editor scene is:

```text
CubeActor
  -> RootComponent [Root]
    -> CubeComponent
```

The renderer traverses World, Level, Actor, and Component membership, finds visible cube
components, and draws each cube from `GetWorldTransform()`. Parent attachment and Actor Transform
therefore affect the viewport without editor-specific synchronization.

## PicoRender

`PicoRender` owns the initial OpenGL 3.3 implementation:

- Runtime loading of the OpenGL procedures used by Pico
- Shader compilation and program linking
- Cube vertex and index buffers
- Ground-grid geometry
- Color texture and depth-stencil framebuffer
- World traversal and draw submission

The editor supplies the active OpenGL context through GLFW. `PicoCore`, `PicoObject`, and
`PicoEngine` do not depend on GLFW, ImGui, or OpenGL.

The framebuffer is resized to the current ImGui viewport panel and presented as a texture. Depth
testing and a simple directional-light shader make the cube spatially readable.

## Editor Camera

The editor camera is viewport state rather than a game object. The viewport supports:

- Right-drag orbit
- Middle-drag pan
- Mouse-wheel zoom

A future `PCameraComponent` will represent cameras owned by the game scene; it does not replace the
editor observation camera.

## Editor Scale

PicoEditor now starts maximized, uses a clear system interface font when available, and scales
fonts, spacing, controls, and toolbar height together. The default scale is `1.4`.

It can be overridden at launch:

```powershell
PicoEditor.exe MyGame.pico -uiscale=1.6
```

Accepted values are clamped between `0.75` and `2.5`.

## Deferred Work

- Static-mesh assets and model importing
- Materials, textures, and PBR lighting
- Selection outlines and transform gizmos
- Render-scene caching and draw batching
- Runtime game window and camera component
- Graphics API abstraction and ray tracing
