# Month 04.8 Viewport Interaction

This milestone connects the rendered scene to the editor selection model and replaces the small
orbit camera with an Unreal-style fly camera.

## Picking Buffer

The scene framebuffer now has three attachments:

```text
Color Attachment 0   -> RGBA8 visible scene
Color Attachment 1   -> R32UI component picking ID
Depth/Stencil        -> shared depth test
```

The grid writes picking ID `0`. Every visible renderable component receives a sequential nonzero ID
for the current frame. The renderer stores:

```text
PickingId -> FObjectHandle
```

The ID is intentionally transient. Runtime handles are not encoded directly into pixels, and no
pointer or picking ID is persisted. Future Sphere and StaticMesh draws only need to register their
component handle and write the assigned ID through the same shader output.

`FSceneViewportRenderer::Pick` reads one integer pixel from attachment 1 and returns the mapped
handle. The shared depth buffer guarantees that overlapping primitives select the front-most
rendered component.

## Editor Selection

The ImGui image uses vertically flipped UVs, so mouse coordinates are normalized inside the image,
scaled for the render-target resolution, and converted back to OpenGL's bottom-left pixel origin.

Left-clicking visible geometry resolves the returned component handle through the Object Registry
and promotes it to its owning Actor. Clicking empty space clears selection. This matches the level
editor rule that viewport geometry normally selects an Actor, while the Outliner can select one
specific component.

Selection does not alter the component's material color. A selected Actor draws a white wireframe
around all of its visible CubeComponents; a component selected in the Outliner draws only its own
wireframe. This is deliberately a small bounds-style visualization rather than a post-process
silhouette.

## Fly Camera

The editor camera is represented by:

```text
CameraPosition
CameraYawDegrees
CameraPitchDegrees
CameraMoveSpeed
```

Pressing the right mouse button inside the Viewport disables and captures the GLFW cursor until the
button is released. Supported platforms use raw mouse motion, and camera rotation reads GLFW cursor
deltas directly instead of DPI-scaled ImGui coordinates:

```text
Mouse delta -> yaw and pitch
W/S         -> forward and backward
A/D         -> left and right
Q/E         -> down and up
Shift       -> 4x movement speed
Mouse wheel -> adjust base speed
```

Movement is normalized before applying speed, preventing diagonal input from moving faster.
`ImGuiIO::DeltaTime` is clamped to limit jumps after a breakpoint or stalled frame. Camera movement
is editor-only state and does not create transactions or modify `.pworld`.

## Current Scope

This interaction layer supports one selected Actor or component. It does not yet include
multi-selection, marquee selection, transform gizmos, component cycling from the viewport, or a
post-process silhouette. Those can build on the same selection handle without changing the picking
contract.
