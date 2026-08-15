# Initial Windows Development Packaging

Pico now has a first automated `Build Target -> Collect -> Stage -> Validate -> Smoke Test`
pipeline. It follows the same broad boundary as Unreal's Build, Cook, Stage, and Package flow while
keeping the first implementation small enough to study.

## Entry Points

From PicoEditor:

```text
File -> Package Project -> Windows (Development)
```

The editor opens a compact settings window for the output root, package name, replacement policy,
and two-frame smoke-test toggle. It requires the current World to be saved and Standalone Play to be
stopped, then launches `PicoPackager` as a separate process. Progress and the final Stage path are
reported through Message Log. Closing the editor does not make packaging code part of the runtime or
editor process.

From PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\PackageProject.ps1
```

Use `-StageName PicoSandbox_TestPackage` to create a separately named Stage. The Stage name changes
the output folder only; it does not rename the project descriptor, target, executable, or runtime
project identity.

Or invoke the tool directly:

```powershell
.\Build\Release\PicoPackager.exe `
  -project=.\Projects\PicoSandbox\PicoSandbox.pico `
  -receipt=.\Build\Release\PicoSandboxGame.targetreceipt `
  -output=.\Projects\PicoSandbox\Saved\StagedBuilds `
  -stagename=PicoSandbox_TestPackage `
  -engineroot=. `
  -profile=Development `
  -smoke
```

## Output

```text
Saved/StagedBuilds/PicoSandbox-Windows-Development/
  PicoStage.manifest
  PackageReport.ini
  Binaries/PicoSandboxGame.exe
  Engine/PicoEngine.root
  Engine/Config/Pico.ini
  PicoSandbox/PicoSandbox.pico
  PicoSandbox/Config/Pico.ini
  PicoSandbox/Content/<native assets>
```

The Stage is assembled in a unique internal `.PicoStaging-<name>-<nonce>` directory. That working
directory is removed after success or failure and is never presented as a second package. Only a
completely collected, validated, and optionally smoke-tested Stage may replace the previous
successful output. Replacement uses a short-lived backup and retry path so a failed package cannot
destroy the last known-good build.

The editor deliberately separates two operations:

- keep the default stable package name and enable `Replace Package` to update that output;
- enter another package name to preserve the old Stage and create a side-by-side output.

If the destination already exists and neither choice is explicit, packaging stops with a clear
error. The editor extracts the first `Error:` line from a failed packager process instead of showing
only the trailing file count.

## Target Receipt

CMake generates `PicoSandboxGame.targetreceipt` next to the project executable:

```ini
[Target]
Name=PicoSandboxGame
Type=Game
Platform=Windows
Configuration=Release
EngineVersion=0.1.0
Executable=PicoSandboxGame.exe

[RuntimeDependencies]
```

Future Client and Server targets use the same format. A backend that needs a DLL, shader directory,
or another runtime file adds an entry instead of teaching the Stage builder about Jolt, OpenGL,
animation, networking, or a particular project:

```ini
Dependency0=Backend.dll|Binaries/Backend.dll|PhysicsBackend
```

## Asset Policy

Development Package V1 collects every registered Pico native extension and preserves its Content
path. It excludes `Content/Source`, `Saved`, `Intermediate`, FBX, glTF, GLB, OBJ, editor files, and
Assimp. This is intentionally correctness-first: dependency-pruned Cook remains a later optimization.

`IPackageContributor` is the extension boundary for future ECS assets, graph bytecode, backend data,
network configuration, and project-specific files. Runtime code does not depend on `PicoPackaging`.

## Validation

Packaging rejects unsafe relative paths, duplicate Stage destinations, symbolic links, missing
target files, Engine/project version mismatches, missing default map/profile assets, and forbidden
source directories. `PackageReport.ini` records every staged file, its reason, and size without
embedding developer-machine source paths.

With `-smoke`, the staged executable is launched from an unrelated temporary working directory for
two frames. This verifies executable-relative Stage discovery, Engine/project identity, config,
AssetRegistry scanning, default map loading, and normal shutdown.

Windows game targets use the GUI subsystem, so normal editor Play and packaged execution do not open
a separate console or PowerShell-style frame-statistics window. Diagnostics remain available through
the runtime UI, logs, and smoke-test exit code.

The accepted PicoSandbox Stage contains 55 files, excludes all import sources, and completes the
two-frame repository-external probe with exit code zero.

## Remaining Packaging Work

- dependency graph Cook rooted at selected maps and project settings;
- Development versus true compiler-level Shipping profiles;
- Client and Dedicated Server receipts after networking;
- optional archive/ZIP and clean-machine Visual C++ runtime verification;
- PicoTask progress/cancellation integration.

These additions extend Target Receipts and Contributors; they do not replace the V1 Stage pipeline.
