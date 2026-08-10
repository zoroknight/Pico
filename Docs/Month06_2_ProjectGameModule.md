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
  -> select and initialize a reflected PGameInstance
  -> load the configured World
  -> PGameInstance::OnWorldInitialized
  -> World and project Actor ticks
```

Engine classes are registered by `FEngineLoop`. The project module starts after the object system
and engine classes exist, but before World deserialization needs to resolve project class names.
The project module returns a `PClass` derived from `PGameInstance`. `FGameEngine` constructs it through
`NewObject` before map loading, roots it across map replacement, and sends explicit World lifecycle
notifications after a replacement commits.

Shutdown cleans up and destroys the GameInstance, tears down the active World and object system, and
only then shuts down the project module so native project code remains available during object cleanup.

## Targets

- `PicoGameRuntime` owns the reusable GLFW window, input callbacks, render loop, and guarded exit.
- `PicoGame` remains a generic runtime that does not contain project code.
- `PicoSandboxModule` contains project reflection registration, GameInstance, and gameplay classes.
- `PicoSandboxGame` links the reusable runtime and Sandbox module into a project executable.

The editor reads `[Game] Executable` from the project `Config/Pico.ini`, validates it as a file name,
and launches that executable with the current project descriptor and `/Game/...` World path.

## Minimal Gameplay

`PSandboxGameInstance` spawns a reflected `PSandboxPawn` from `OnWorldInitialized` and gives it the runtime input
system. The Pawn creates a static-mesh root and consumes `MoveForward` and `MoveRight` during the
existing Actor Tick. This proves that project code, reflection, World ownership, project assets,
input mappings, and rendering all participate in one runtime path.

Dynamic game-module loading, editor loading of project-native types, and hot reload remain deferred.
The current static project target avoids crossing singleton object registries and C++ ABI boundaries
until Pico has shared-library rules designed for them.
