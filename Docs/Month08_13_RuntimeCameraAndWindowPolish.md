# Runtime Window, FPS, and Third-Person Camera Polish

This increment hardens the development package and the third-person camera without coupling Gameplay
to GLFW, Jolt, or a particular renderer.

## Implemented Boundaries

- `PicoGame` and `PicoSandboxGame` use the Windows GUI subsystem. Normal Play and packaged execution
  no longer open a separate console window; a future Dedicated Server target may choose a console
  subsystem independently.
- `FEngineLoop` exposes smoothed frame time and FPS. PicoEditor displays them in the top bar and keeps
  the presentation toggle under `View -> Frame Rate`.
- `PController` owns reflected `ViewPitchMin` and `ViewPitchMax` limits. Sandbox uses `-75` and `+55`
  degrees, so the upper orbit stops at 145 degrees measured from world `+Z` instead of passing under
  the character.
- `PSpringArmComponent` owns reflected `Do Collision Test` and `Probe Size` settings. It asks the
  World's collision-query interface for a sphere sweep from arm origin to the ideal camera endpoint,
  ignores PrimitiveComponents owned by the same Actor, and retracts to the first blocking hit.

This mirrors the useful architectural part of UE's SpringArm: the camera rig requests a collision
query through the World boundary and consumes a hit result. It does not know that Pico currently uses
Jolt, and turning collision off returns the exact ideal endpoint. Camera lag remains future work.

## Visual Acceptance

1. Launch PicoEditor with `Projects/PicoSandbox/PicoSandbox.pico`.
   Purpose: verify the project descriptor, saved World, and editor frame loop use the normal path.
2. Toggle `View -> Frame Rate` twice.
   Purpose: verify the top-bar FPS/frame-time readout is optional and does not change simulation.
3. Play the saved World and move the mouse vertically to both limits.
   Purpose: verify pitch stops before the camera can orbit under the character and remains smooth at
   the clamp.
4. Put a wall between the character and its desired camera position.
   Purpose: verify `CameraBoom` retracts in front of blocking geometry instead of rendering through it.
5. Stop Play, open the playable Actor Blueprint, select `CameraBoom`, disable `Do Collision Test`, and
   Play again near the same wall.
   Purpose: verify collision is a data-driven component policy rather than hard-coded runtime behavior.
6. Re-enable collision, change `Probe Size`, save, reopen, and Play.
   Purpose: verify reflected editing, Blueprint defaults, serialization, and runtime construction all
   preserve the setting.
7. Package with a new Package Name and launch `Binaries/PicoSandboxGame.exe`.
   Purpose: verify a side-by-side Stage starts without the repository working directory and without a
   console window.
8. Package the stable default name with `Replace Package` enabled.
   Purpose: verify intentional replacement updates the final output only after validation and smoke
   testing; the previous successful package remains intact if packaging fails.

## Automated Acceptance

- Engine tests use a deterministic fake collision query to verify the collision toggle and endpoint
  selection without loading Jolt.
- Physics tests use a real Jolt wall and sphere sweep to verify backend integration.
- Sandbox tests verify the project Controller's pitch policy.
- The complete Release suite currently passes all 17 CTest targets, and the accepted Development
  Stage contains 55 files and completes the repository-external two-frame smoke test.
