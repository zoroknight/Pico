# Project Game Module and Runtime Target

This milestone turns the standalone runtime from a generic World viewer into a project-owned game
target. It keeps the small static-linking model while preserving the same lifecycle boundary that a
future DLL-based module loader can use.

## Startup Order

```text
PicoSandboxGame main
  -> RunPicoGame
  -> FGameEngine::PreInit
  -> FEngineLoop::Init
  -> IGameModule::StartupModule
  -> load the configured World
  -> create and initialize FGameInstance
  -> World and project Actor ticks
```

Engine classes are registered by `FEngineLoop`. The project module starts after the object system
and engine classes exist, but before World deserialization needs to resolve project class names.
The game instance starts only after the active World has been replaced by the configured map.

Shutdown reverses the ownership order: GameInstance, project module, active World, object system.

## Targets

- `PicoGameRuntime` owns the reusable GLFW window, input callbacks, render loop, and guarded exit.
- `PicoGame` remains a generic runtime that does not contain project code.
- `PicoSandboxModule` contains project reflection registration, GameInstance, and gameplay classes.
- `PicoSandboxGame` links the reusable runtime and Sandbox module into a project executable.

The editor reads `[Game] Executable` from the project `Config/Pico.ini`, validates it as a file name,
and launches that executable with the current project descriptor and `/Game/...` World path.

## Minimal Gameplay

`FSandboxGameInstance` spawns a reflected `PSandboxPawn` after map load and gives it the runtime input
system. The Pawn creates a static-mesh root and consumes `MoveForward` and `MoveRight` during the
existing Actor Tick. This proves that project code, reflection, World ownership, project assets,
input mappings, and rendering all participate in one runtime path.

Dynamic game-module loading, editor loading of project-native types, and hot reload remain deferred.
The current static project target avoids crossing singleton object registries and C++ ABI boundaries
until Pico has shared-library rules designed for them.
