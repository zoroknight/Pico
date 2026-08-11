# Month 7.5: Development, Installed, and Staged Runtime Layouts

This hardening task removes Pico Runtime's dependency on the source repository layout. It follows
the same broad separation used by Unreal Engine: source development, an installed Engine, and a
staged game are distinct runtime layouts.

## Layout Modes

`FPaths` now exposes `EEngineLayoutMode`:

```text
Development  source repository with CMakeLists.txt, Source/Runtime/Core, and Config/Pico.ini
Installed    Engine root with PicoEngine.root and Config/Pico.ini
Staged       game Stage root driven by PicoStage.manifest
```

Development lookup preserves the existing workflow. Installed and Staged layouts never require
`CMakeLists.txt`, `Source`, Editor files, or the original repository.

Explicit command-line roots have strict behavior:

```text
-engineroot=<installed-or-development-engine-root>
-stageroot=<stage-root>
```

The two switches are mutually exclusive. An invalid explicit root fails immediately instead of
silently falling back to a development tree.

## Installed Engine Marker

An installed Engine contains `PicoEngine.root`:

```ini
[Engine]
Name=Pico
Version=0.1.0
LayoutVersion=1
```

The marker and `Config/Pico.ini` are sufficient to identify the Engine root. `Version` is checked
against the running binary during `PreInit`.

## Stage Manifest

The Stage root contains `PicoStage.manifest`:

```ini
[Stage]
LayoutVersion=1
EngineVersion=0.1.0
ProjectName=PicoSandbox
EngineRelativePath=Engine
ProjectRelativePath=PicoSandbox/PicoSandbox.pico
```

Both paths must be relative and remain inside the Stage root after canonicalization. The staged
Engine must have a valid installed marker, its version must match the manifest and binary, and the
project descriptor name must match `ProjectName`. A project path outside Stage is rejected.

The reusable game launch layer ignores its compile-time development project fallback when the EXE
is inside a Stage or `-stageroot` is present. This lets the manifest provide the project location
without embedding a developer-machine absolute path in runtime behavior.

## Resolution Order

```text
explicit -stageroot
explicit -engineroot
PicoStage.manifest discovered from the executable directory
Development/Installed Engine search from working directory and executable directory
```

Stage discovery starts from the executable, so launching from an unrelated working directory does
not change the selected Engine or Project.

## Verification

Core tests cover:

- Installed Engine recognition without Source;
- explicit invalid root rejection without fallback;
- explicit and executable-relative Stage discovery;
- manifest project inference;
- Engine and project identity/version metadata;
- rejection of projects outside Stage;
- restoration of the normal Development layout.

The Release probe staged only:

```text
Stage/
  PicoStage.manifest
  Binaries/PicoSandboxGame.exe
  Engine/PicoEngine.root
  Engine/Config/Pico.ini
  PicoSandbox/PicoSandbox.pico
  PicoSandbox/Config/
  PicoSandbox/Content/
```

It was launched from outside Stage with no project argument. Runtime logged `layout=Staged`, loaded
13 assets and `EditorWorld.pworld`, ticked two frames, and exited with code zero. The Stage contained
neither `Source` nor `CMakeLists.txt`.

This task establishes path and identity rules only. Cook dependency discovery, automated Stage
generation, receipts, archives, Development/Shipping profiles, and the editor Package command remain
in the planned packaging milestone.
