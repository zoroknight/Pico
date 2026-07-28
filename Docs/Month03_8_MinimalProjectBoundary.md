# Month 03.8 Minimal Project Boundary

This milestone separates engine installation paths from user project paths before Pico adds scene
serialization, asset importing, generated reflection code, or packaging.

## Two Roots

Pico no longer calls the engine repository a project. Engine-only tools initialize:

```text
EngineRoot  = <Pico repository>
ProjectRoot = None
```

Opening a project descriptor initializes:

```text
EngineRoot  = <Pico repository>
ProjectFile = <project>/MyGame.pico
ProjectRoot = <project>
```

The project may be inside the repository as an engine-maintained sample or in an unrelated external
directory. Runtime code receives both as normalized absolute paths.

## Project Descriptor

The minimal descriptor uses Pico's existing INI parser:

```ini
[Project]
Name=MyGame
FileVersion=1
EngineVersion=0.1.0
```

`FProjectDescriptor` validates the extension, file version, and project name. `FEngineLoop` uses the
descriptor name for `FApp`, and loads `Config/Pico.ini` from the project when present. Engine config
is the fallback.

Projects can be passed either as a positional argument or an explicit switch:

```powershell
PicoEditor.exe E:\PicoProjects\MyGame\MyGame.pico
PicoEditor.exe -project=E:\PicoProjects\MyGame\MyGame.pico
```

## Project Directories

The minimal writable project shape is:

```text
MyGame/
  MyGame.pico
  Config/
  Source/
  Content/
  Intermediate/
  Saved/
```

- `Source` is authored C++ and is never an automatic editor write target.
- `Content` is persistent project content such as future `.pworld`, `.pobj`, and `.pmesh` files.
- `Intermediate` is replaceable generated data.
- `Saved` is logs, tests, autosaves, and local editor state.

`Binaries` and `DerivedDataCache` are reserved by the sample `.gitignore` for later milestones.

## Write Boundary

Editor and tool code request paths through:

```cpp
FPaths::TryGetProjectWritePath(
    EProjectWriteRoot::Content,
    "Maps/Main.pworld",
    OutPath);
```

Only descendants of `Content`, `Intermediate`, and `Saved` are approved. Absolute paths, `..`
traversal, project `Source`, and Engine `Source` are rejected. Paths are weakly canonicalized so an
existing symlink cannot silently redirect a write outside its approved root.

The API validates paths but does not itself write files. Writers such as the PicoSandbox serializer
must call `IsProjectWritePath` before creating directories or opening output files.

## Deferred Work

- Independent project CMake and game-module DLL loading
- PicoHeaderTool and generated C++ under `Intermediate`
- Scene and asset database formats
- Content cooking and packaging
- Project creation UI and templates
