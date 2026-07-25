# Pico Month 01: Core And Engine Loop

The first month focuses on building the smallest useful engine skeleton. The goal is not to copy Unreal Engine's full startup complexity, but to understand why an engine centralizes startup, ticking, and shutdown.

## Milestone 1

Run `PicoLaunch` and observe this lifecycle:

```text
main
  -> Pico::GuardedMain
    -> FEngineLoop::PreInit
    -> FEngineLoop::Init
    -> FEngineLoop::Tick
    -> FEngineLoop::Exit
```

Expected command:

```powershell
.\Pico\Scripts\BuildWindows.ps1
.\Pico\Build\Debug\PicoLaunch.exe -frames=5
```

By default Pico keeps running, like Unreal Editor. Use `-frames=N` for smoke tests:

```powershell
.\Pico\Build\Debug\PicoLaunch.exe -frames=5
```

Use `-maxfps=N` to cap the main loop:

```powershell
.\Pico\Build\Debug\PicoLaunch.exe -maxfps=60
```

Pico also reads default engine settings from:

```text
Config/Pico.ini
```

Pico resolves this path through `FPaths`, so the executable can be launched from different working directories and still find the project config.

Currently supported values:

```ini
[Engine]
MaxFrameCount=-1
MaxFPS=60
```

Command line arguments override config values. For example, `-maxfps=120` wins over `MaxFPS=60` in `Pico.ini`.

Frame limits have explicit boundary behavior:

- `-frames=-1` runs until another system requests exit.
- `-frames=0` initializes and shuts down without ticking.
- `-frames=N`, where `N` is positive, runs exactly `N` frames.
- Values below `-1` fail during `PreInit` instead of entering an accidental infinite loop.
- `-maxfps=0` disables the frame cap, while negative or non-finite values fail during `PreInit`.

`GuardedMain` has one exit path. Once `FEngineLoop` has been created, `Exit` is called after successful execution, initialization failure, or an exception. `Exit` is idempotent so later subsystems can safely add phase-aware cleanup.

`FPaths` treats `Config/Pico.ini` as the Pico project marker. An unrelated working directory that merely contains a `CMakeLists.txt` cannot replace the Pico project root. This keeps configuration discovery stable when the executable is launched from another source repository.

Visual Studio workflow:

```text
Folder view:
1. Double-click Scripts\OpenFolder.bat
2. Visual Studio opens the Pico root folder
3. Solution Explorer shows Build, Docs, Scripts, Source, and CMakeLists.txt

Solution view:
1. Double-click Scripts\GenerateProjectFiles.bat
2. Open Build\Pico.sln, or double-click Scripts\OpenSolution.bat
3. Select PicoLaunch.vcxproj as the startup target
4. Build and run from Visual Studio
```

## Why This Shape

`Launch` owns the executable entry point. This keeps platform and process startup separate from the engine runtime.

`GuardedMain` wraps the engine loop. In Unreal, this area is where crash handling, error reporting, command line setup, and high-level lifecycle control gather.

`PreInit` prepares global process state. Pico currently parses command line arguments and initializes `FApp`.

`Init` creates runtime systems. Pico currently starts the frame timer. Later this will initialize the object system, reflection registry, world, renderer, asset manager, and editor mode.

`Tick` advances one frame. Pico currently updates frame count and time. Later this will drive world ticking, component ticking, rendering, networking, and editor updates.

`Exit` releases runtime systems in reverse order. Pico currently logs the final frame. Later this will flush logs, destroy worlds, release renderer resources, and shut down subsystems.

## Current Core Types

`FApp` stores global application state and owns its copy of the project name:

- project name
- exit request flag
- frame counter

`FCommandLine` stores startup arguments and supports simple switches such as `-frames=5`.

`FConfigFile` loads a minimal INI-style config file. Pico uses it during `PreInit`, before higher-level systems are initialized.

`FPaths` initializes early in `PreInit` and resolves important directories such as the executable path, project root, and config directory.

`FLog` prints timestamped log lines with a category and verbosity, such as `[LogEngine][Info]`.

`PICO_LOG(Category, Level, ...)` is a small UE-style logging macro. It lets each subsystem identify its own output:

```cpp
PICO_LOG(LogEngine, Info, "Init");
PICO_LOG(LogConfig, Warning, "{} was not found", Path);
```

`FFrameTimer` tracks delta time, total runtime, average frame time, average FPS, and can sleep at the end of a frame to respect a max FPS cap.

`FEngineLoop` coordinates the lifecycle.

`FEngineLoop` lives in the `PicoEngine` module instead of `PicoCore`. This keeps the lowest Core layer focused on reusable primitives such as logging, timing, command line parsing, types, platform macros, and assertions.

Current module dependency direction:

```text
PicoLaunch
  -> PicoEngine
    -> PicoObject
      -> PicoCore
```

`PicoObject` was added in Month 02. The dependency still preserves the Month 01 rule that lower-level modules never depend on `PicoEngine` or `PicoLaunch`.

`Types.h` defines explicit-width aliases such as `int32`, `uint64`, `float32`, and `float64`. Engine code should prefer these where serialized, reflected, or networked data needs stable sizes.

`Platform.h` defines the first platform/compiler/config macros, such as `PICO_PLATFORM_WINDOWS`, `PICO_COMPILER_MSVC`, and `PICO_CONFIG_DEBUG`.

`Assert.h` defines the first development checks:

- `PICO_CHECK(Expression)` aborts on failure.
- `PICO_CHECK_MSG(Expression, ...)` logs a formatted message and aborts.
- `PICO_ENSURE(Expression)` logs and returns `false`, but lets execution continue.

`FFrameTimer` uses an exponential moving average similar in spirit to Unreal's `CalculateFPSTimings`:

```text
AverageFrameTimeMS = AverageFrameTimeMS * 0.9 + CurrentFrameTimeMS * 0.1
AverageFPS = 1000 / AverageFrameTimeMS
```

## Automated Tests

All test-only code lives under the top-level `Tests` directory, separate from runtime code under `Source`.

`PicoCoreTests` links only against `PicoCore`. It verifies command line parsing, strict config parsing, project-name ownership, project path discovery from a foreign CMake working directory, and basic frame timer invariants.

`PicoEngineTests` links against `PicoEngine`. It covers the original two-frame `GuardedMain` lifecycle, a zero-frame lifecycle, and rejection of an invalid negative frame limit. These cases verify that command line overrides, ticking, initialization failure, and unified exit cleanup work together.

The test executable returns `0` when every check passes and `1` when any check fails. CMake also registers it with CTest, so it can be run directly or through:

```powershell
ctest --test-dir .\Build -C Debug --output-on-failure
```

Keeping `PicoCoreTests` dependent on `PicoCore` alone also checks the module boundary: a Core test must not accidentally require `PicoEngine` or `PicoLaunch`.

## Next Study Questions

1. What belongs in `PreInit` instead of `Init`?
2. Why should `Launch` depend on `Core`, but `Core` should not depend on `Launch`?
3. Which future systems must be initialized before `World` exists?
4. How does a centralized Tick make Actor, Component, Render, and Network systems easier to reason about?

## Next Implementation Steps

The object identity and metadata foundation continues in `Docs/Month02_ObjectReflectionSerialization.md`.
