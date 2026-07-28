# Month 03.12 GLAD Integration

This milestone replaces Pico's hand-written OpenGL procedure table with a generated GLAD loader.
It is an implementation cleanup: the editor viewport, scene traversal, shaders, geometry, and
framebuffer behavior remain unchanged.

## Startup Flow

The OpenGL startup path is now:

```text
GLFW creates an OpenGL 3.3 Core context
  -> PicoEditor makes the context current
  -> PicoEditor passes glfwGetProcAddress to PicoRender
  -> PicoRender calls gladLoadGL
  -> PicoRender verifies GLAD_GL_VERSION_3_3
  -> FSceneViewportRenderer creates shaders and geometry
```

GLFW remains responsible for the native window and OpenGL context. GLAD only declares OpenGL API
entry points and fills their addresses for the active driver.

## Removed Manual Work

`FSceneViewportRenderer` no longer owns:

- Hand-written OpenGL constants
- 36 function-pointer type declarations
- 36 function-pointer fields
- Per-function `LoadProcedure` calls

Rendering code now calls `glCreateShader`, `glGenVertexArrays`, `glBindFramebuffer`, and the other
OpenGL functions directly through GLAD's generated declarations.

## Module Boundary

GLAD is vendored in `ThirdParty/GLAD` and built as its own static CMake target:

```text
PicoEditor
  -> PicoRender
       -> glad
```

The dependency is private to `PicoRender`. `PicoCore`, `PicoObject`, `PicoEngine`, reflected game
objects, and project C++ code do not include GLAD or OpenGL headers.

The generated OpenGL 3.3 files were promoted from the GLAD sources already included with Pico's
vendored GLFW. Their generation options and provenance are recorded in `ThirdParty/GLAD/README.md`.

## Failure Behavior

Renderer initialization fails with a clear log message when:

- No procedure loader is supplied
- GLAD cannot load the active context
- The driver does not provide OpenGL 3.3
- Shader or geometry initialization fails

On success, Pico logs the OpenGL version reported by GLAD.
