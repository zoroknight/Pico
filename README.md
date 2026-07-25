# Pico

Pico is a small learning-oriented C++ engine inspired by Unreal Engine architecture.

## Visual Studio Workflow

There are two useful ways to open Pico in Visual Studio 2022.

### Folder View

Use this for daily source browsing and editing:

```text
Scripts\OpenFolder.bat
```

This opens the Pico root folder directly, so Solution Explorer shows the physical project layout:

```text
Pico
  Build
  Docs
  Scripts
  Source
  CMakeLists.txt
```

### Solution View

Use this when you want the traditional Visual Studio solution/project view:

```text
Scripts\GenerateProjectFiles.bat
Scripts\OpenSolution.bat
```

This opens:

```text
Build\Pico.sln
```

In this mode Visual Studio shows generated projects such as `PicoCore` and `PicoLaunch` rather than the physical folder tree.

## Build

From the Pico root:

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Debug --target PicoLaunch
.\Build\Debug\PicoLaunch.exe -frames=5
```

## Reflection Sandbox

Run the console walkthrough:

```powershell
cmake --build Build --config Debug --target PicoReflectionDemo
.\Build\Debug\PicoReflectionDemo.exe
```

Run the visual reflection and serialization inspector:

```powershell
cmake --build Build --config Debug --target PicoInspector
.\Build\Debug\PicoInspector.exe
```

See `Docs/ReflectionAuthoringGuide.md` for the ordered type-authoring walkthrough.
