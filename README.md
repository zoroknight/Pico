# Pico

[English](README.md) | [简体中文](README.zh-CN.md)

Pico is a small learning-oriented C++ engine inspired by Unreal Engine architecture.

## Requirements

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 with the **Desktop development with C++** workload
- MSVC v143 and a Windows 10/11 SDK
- CMake 3.22 or newer
- Git for Windows

GLFW and Dear ImGui are included in `ThirdParty`, so a fresh checkout does not need to download additional engine dependencies.

## Reproduce on Windows

Clone the private repository, then run the setup script from the Pico root:

```powershell
git clone https://github.com/<your-user-name>/Pico.git
Set-Location Pico
powershell -ExecutionPolicy Bypass -File .\Scripts\SetupWindows.ps1
```

The script checks the environment, generates a Visual Studio 2022 x64 build, compiles every target, and runs all tests. Build outputs are written only to `Build`.

To build and test another configuration:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\SetupWindows.ps1 -Configuration Release
```

After a successful Debug setup:

```powershell
.\Build\Debug\PicoLaunch.exe -frames=5
.\Build\Debug\PicoReflectionDemo.exe
.\Build\Debug\PicoInspector.exe
```

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

## Manual Build

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
