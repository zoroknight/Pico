# Month 7.4: MatchState, Gameplay Events, and Editor Bindings

This milestone finishes Pico's first UE-inspired local Gameplay Framework loop. It adds a small
match state machine, native lifecycle events, a reflected dynamic event example, and a formal
editor workflow for authoring persistent event bindings.

## Runtime Responsibilities

`PGameModeBase` owns the authority to change match state. `PGameStateBase` stores the observable
state and elapsed match time. This mirrors the important UE boundary: rules decide on GameMode,
while shared match data lives on GameState and can later be replicated.

```text
EnteringMap
  -> WaitingToStart
    -> InProgress
      -> WaitingPostMatch

Any active state -> Aborted
World teardown   -> LeavingMap
```

`StartPlay` starts the match once a PlayerState exists. The GameState clock advances only while the
state is `InProgress` and uses `PostUpdateWork`, leaving room for later input, movement, physics,
network, and animation phases.

Native lifecycle delegates expose completed state changes without making the reflected event system
responsible for engine-internal communication:

- `PGameModeBase::OnPostLoginEvent`
- `PGameModeBase::OnLogoutEvent`
- `PGameModeBase::OnMatchStateChanged`
- `PGameStateBase::OnMatchStateChanged`
- `PController::OnPossessedPawnChanged`

`PPlayerStart::OnPlayerSpawnedEvent` is the scene-authored dynamic event example. Its signature is
`void(PPawn*)`. The Callable reflected function `RecordPlayerSpawn(PPawn*)` increments a transient
counter, making successful broadcasts visible without adding game-specific code to the engine.

## Editor Events & Bindings

Select an Actor or Component with a reflected dynamic multicast property. Details shows a separate
`Events & Bindings` section instead of treating the delegate as an ordinary value.

The panel can:

- list target object path and reflected function name;
- choose a target object from the same World;
- show only Callable `PFunction` entries with an exactly compatible signature;
- add a unique binding or remove one precise binding;
- diagnose an unresolved target;
- mark the World dirty and participate in Undo/Redo;
- persist the binding through `.pworld` SceneId/path reference repair.

The editor never stores a raw pointer or runtime `FObjectHandle` in the scene file. It serializes a
stable target identity and function name, reconstructs all objects first, then repairs the binding.

## Visual Acceptance

1. Open `Projects/PicoSandbox/PicoSandbox.pico` in `PicoEditor`.
2. Create or select a `Player Start` Actor and select the Actor row in Scene Outliner.
3. Expand `Events & Bindings`, then expand `OnPlayerSpawnedEvent`.
4. Click `Add Binding...`.
5. Choose that same PlayerStart as `Target Object`.
6. Choose `RecordPlayerSpawn` as `Target Function`, then click `Add`.
7. Confirm the binding count becomes one. Press `Ctrl+Z` and `Ctrl+Y` to verify removal/restoration.
8. Save, close the map, reopen it, and confirm the binding still appears.
9. Press Play. In `Gameplay Debug`, confirm Match is `InProgress`, PlayerStart bindings is one, and
   broadcasts observed is one.
10. Click `Restart Player`. The Pawn handle changes and the broadcast count becomes two.
11. Click `UnPossess`, `Possess Last Pawn`, and `Restart Player`; observe the object-chain events.
12. Click `End Match`; Match becomes `WaitingPostMatch` and elapsed time stops increasing.
13. Reload the map; GameInstance/LocalPlayer remain while World-owned objects are replaced and the
    new match returns to `InProgress`.

Each check has a distinct purpose: steps 2-6 verify metadata filtering and authoring; step 7 verifies
transactions; step 8 verifies stable serialization; steps 9-10 prove runtime invocation; step 11
proves control lifecycle delegates; step 12 proves state ownership and clock gating; step 13 proves
map-lifetime boundaries.

## Automated Coverage

`PicoEngineTests` covers legal state transitions, duplicate-transition rejection, elapsed-time
gating, native Possess notifications, dynamic invocation, transient counter reset, and binding
repair after transactional World replacement. `PicoGameTests` covers automatic standalone startup
into `InProgress`, clock advancement, and the persistent LocalPlayer/current-World replacement path.

## Boundary For Later Networking

The properties stored by GameState are marked `Replicated`, but this milestone does not send network
packets. Month 6 will make GameMode server-only and replicate GameState to clients. The current API
already prevents clients from needing to own match rules: they will observe GameState and its state
change notification instead.

## Early External Runtime Probe

The Release executable and only the project descriptor, `Config`, and `Content` were staged at:

```text
C:/Users/Jarvis/AppData/Local/Temp/PicoExternalProbe-20260811-021947
```

The first two-frame run failed during `PreInit`. `FPaths::IsEngineRoot` currently recognizes an
engine installation only when it contains all three development-tree markers:

```text
CMakeLists.txt
Source/Runtime/Core/
Config/Pico.ini
```

After adding empty `CMakeLists.txt` and `Source/Runtime/Core` markers plus the real engine `Config`,
the same staged executable loaded 13 assets, loaded `EditorWorld.pworld`, rendered through GLAD,
ticked exactly two frames, and exited with code zero. No missing DLL, Editor, shader source, or
project Source dependency appeared after root discovery.

The issue was resolved in the immediate hardening task: Pico now distinguishes Development,
Installed, and Staged layouts through `PicoEngine.root`, `PicoStage.manifest`, `-engineroot`, and
`-stageroot`. A second Release probe ran without Source or CMake files. See
[`Month07_5_DevelopmentInstalledAndStagedLayouts.md`](Month07_5_DevelopmentInstalledAndStagedLayouts.md).
