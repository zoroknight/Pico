# Month 8.4: Skeletal Animation

This milestone adds Pico's first complete skeletal-animation path while preserving the backend
boundaries required by later rendering, networking, Montage, and AbilityTask work.

## Runtime Chain

```text
FSkeletonData + FSkeletalMeshData + FAnimationClipData
  -> PAnimInstance selects Idle / Walk / Jump and samples FSkeletonPose
  -> PSkeletalMeshComponent performs CPU skinning
  -> FSkinnedMeshRenderData (ordinary vertices, indices, sections, revision)
  -> PicoRender uploads a per-component dynamic VBO
```

`PicoAsset` owns only native Pico data. `PAnimInstance` owns playback time and state selection.
`PSkeletalMeshComponent` is a reflected primitive component and owns its AnimInstance through the
normal `NewObject`, Outer, object-handle, reference-collector, and destruction chain. OpenGL handles
exist only in `PicoRender`; Assimp objects exist only in `PicoAssetImport`.

CPU skinning is intentional for this learning milestone. It makes pose output renderer-neutral and
keeps the first implementation testable. A future GPU skinning path can consume the same skeleton,
pose, mesh, and render submission boundary without changing Gameplay or serialized assets.

## Native Assets

Pico now recognizes and caches:

- `.pskeleton`: ordered bones, parent indices, reference local transforms, inverse bind matrices.
- `.pskeletalmesh`: weighted vertices, indices, sections, bounds, and skeleton asset reference.
- `.panimation`: duration, looping, bone tracks, root-bone index, and reserved Notify ranges.

Loaders validate parent order, duplicate names, finite transforms, index ranges, normalized weights,
section coverage, key ordering, duration, track uniqueness, and trailing archive data. The three
formats are independent CPU assets and contain no source-format or graphics-backend object.

## AnimInstance And State

`PAnimInstance::Update` receives only animation-relevant movement observations:

```text
Falling                         -> Jump
not Falling and GroundSpeed > 5 -> Walk
otherwise                       -> Idle
```

A state change resets playback time, then the selected clip is sampled into local transforms,
component-space transforms, and skinning matrices. This is the small equivalent of UE's separation
between movement state, AnimInstance policy, pose evaluation, and skeletal rendering. Blend spaces,
state-machine graphs, sync groups, additive animation, retargeting, and animation threading remain
future work.

## Root Motion

Animation extracts a frame delta from the configured root bone. `PSkeletalMeshComponent` queues that
delta on `PCharacterMovementComponent`; the next movement tick stores it in
`FCharacterMoveInput::RootMotionDelta` and applies it through swept `MoveComponent`.

Root Motion therefore does not write Actor Transform directly. It collides with walls, reports an
impact, can slide, and remains part of the same replayable input record that future server replay and
client prediction will use. The one-frame queue is explicit because animation currently evaluates in
`PostPhysics`; networking work may later split animation update and pose evaluation more finely.

## Import Boundary

`PicoAssetImport` contains the Assimp adapter for glTF/GLB and experimental FBX input. It converts
coordinates, units, hierarchy, inverse bind matrices, four normalized influences, and animation
channels into Pico native assets. `PicoAssetTool import-skeletal` exposes the conversion as a command.

Assimp is optional at configure time. The preferred offline setup is to extract its source tree to
`ThirdParty/Assimp`, which is the default `PICO_ASSIMP_SOURCE_DIR`:

```powershell
cmake -S . -B Build
cmake --build Build --config Debug --target PicoAssetTool
```

The local source directory is Git-ignored because Assimp 6.0.4 expands to roughly 234 MiB. CMake limits
this integration to the glTF and FBX importers. A different source location can be supplied with
`-DPICO_ASSIMP_SOURCE_DIR=<path>`; a system package and `-DPICO_FETCH_ASSIMP=ON` are fallback options.

Without Assimp, the engine, editor, tests, native assets, procedural animation demo, and renderer all
build normally; the command reports `AssimpUnavailable`. With the local Assimp 6.0.4 source installed,
the real adapter and `PicoAssetTool` have imported Assimp's official skinned glTF and animated FBX
fixtures into `.pskeleton`, `.pskeletalmesh`, and `.panimation` assets in both Debug and Release tests.

## Visual Verification

The normal editor workflow is now documented in
[`Month08_5_SkeletalAssetPreview.md`](Month08_5_SkeletalAssetPreview.md). It imports an external file
directly into a transient Preview World, then writes consistent `/Game` references and native files
under project Content after confirmation. The command-line workflow below remains useful for headless
tests, builds, and future Agent tools.

Build and run:

```powershell
cmake --build BuildWeek4 --config Debug --target PicoSandboxGame
.\BuildWeek4\Debug\PicoSandboxGame.exe
```

The Sandbox character retains the existing cow mesh and adds a two-bone animated totem beside it:

1. Do not press a movement key. The upper body should sway slightly. This verifies Idle selection,
   looping playback, pose sampling, CPU skinning, and dynamic vertex upload.
2. Hold `W`, `A`, `S`, or `D`. The upper body should swing much farther. This verifies that
   CharacterMovement velocity selects Walk rather than a timer or direct key check.
3. Press `Space`. The pose changes to the large Jump motion while MovementMode is Falling, then
   returns to Walk or Idle on landing. This verifies movement-driven state transition and recovery.
4. Press `R`. The replacement Pawn should receive a new SkeletalMeshComponent and AnimInstance and
   animate again. This verifies normal Gameplay reconstruction and object ownership.

Press `F1` during the same test and inspect the separate `Animation` section:

```text
Ground speed: 0.00
Animation state: Idle
Movement mode: Walking
Current clip: Idle
Playback time: ...
```

This deliberately reports movement and animation state separately. `Walking` is the grounded
CharacterMovement mode, not the name of the currently playing animation. At rest, `Ground speed`
should be near zero and `Animation state`/`Current clip` should be `Idle`; while moving above the
selection threshold they should become `Walk`; while airborne the movement mode and animation state
should become `Falling` and `Jump`. This verifies that animation policy consumes movement state
instead of treating the movement-mode label as an animation command.

The procedural fixture deliberately avoids an external model so the runtime chain can always be
verified, even when Assimp is unavailable. The automated importer acceptance additionally uses the
official Assimp glTF and FBX fixtures whenever the local source tree is present.

## Automated Verification

`PicoAnimationTests` covers compatible asset validation, pose sampling, weighted skinning, root-motion
extraction, native file round trips, unified AnimInstance construction, and Idle/Walk/Jump selection.
`PicoCharacterMovementTests` additionally proves a large Root Motion delta is blocked by a wall and
remains present in replayable move input. The Sandbox executable also completes a five-frame OpenGL
runtime smoke test.

## Deliberate Limits

This milestone does not implement GPU skinning, animation compression, retargeting, blend trees,
animation graphs, layered slots, Montage playback, Notify dispatch, IK, Morph Targets, or networked
animation state. Notify data and root-motion extraction are reserved in the asset/runtime boundary.
Montage Lite is the next optional animation extension and must reuse these paths before
`PlayAnimationAndWait` AbilityTask is implemented.
