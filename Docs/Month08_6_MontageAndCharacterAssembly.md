# Month 08.6: Montage Lite and Character Assembly

This stage closes the animation boundary before networking. It follows the UE idea that locomotion clips,
Montages, mesh materials, and gameplay movement are separate systems, while deliberately avoiding a full
Persona, AnimGraph, retargeter, or Montage timeline editor.

## Implemented

- `.panimset` stores stable `/Game` references to one Skeleton and Idle/Walk/Jump clips.
- `.pmontage` stores one slot, clip segments, named sections, next-section links, blend times, and
  Notify/NotifyWindow ranges.
- AssetRegistry and AssetManager discover, validate, load, cache, and invalidate both asset types.
- `PAnimInstance` supports Play, Stop, JumpToSection, section transitions, blend in/out, Notify Begin/End,
  Completed/Interrupted/Cancelled results, pose blending, and Montage root-motion extraction.
- Montage root motion returns through `PCharacterMovementComponent`, then uses the existing
  MoveComponent/Sweep collision path. It never directly writes the Actor transform.
- `PSkeletalMeshComponent` references an AnimationSet and an optional default Montage. The old three clip
  properties remain as a compatibility fallback for existing worlds.
- A skeletal mesh can use the legacy material plus four reflected per-section material overrides.
- Skeletal rendering submits each mesh section separately and resolves its matching override.
- Skeletal import automatically creates an AnimationSet when imported clip names contain Idle, Walk, and Jump.
- Content Browser opens SkeletalMesh, AnimationClip, AnimationSet, and Montage assets in the skeletal preview.
- Skeletal Preview contains a Montage Lab with Play, Stop, Jump To Finish, playback state, section/time, Notify
  events, and end reason. For an ordinary clip it builds a transient two-section Montage, so no authored
  `.pmontage` is required for a quick test.

## Visual verification

1. Start `BuildWeek4/Debug/PicoEditor.exe` with the PicoSandbox project.
2. In Content Browser, double-click the existing `simple_skin.pskeletalmesh` or its animation clip.
3. Confirm the mesh and normal clip playback still work. This verifies that Montage did not replace locomotion.
4. Click `Play Montage`. The state changes to Playing and the log starts with `Played: Start`.
5. Let it play. `PreviewEvent` verifies a one-shot Notify; `PreviewWindow Begin/End` verifies a duration window.
6. Click `Jump To Finish`. The displayed section changes immediately and playback continues from that boundary.
7. Click `Stop Montage`. The pose blends back to locomotion, then the event list reports `Ended: Interrupted`.
8. Play without interruption. The final event is `Ended: Completed`, and it appears exactly once.

After importing a humanoid GLB containing Idle, Walk, and Jump clips, verify the generated `.panimset`, add a
Skeletal Mesh Component to a Character, and assign SkeletalMesh, AnimationSet, and MaterialOverride fields in
Details. Save the `.pworld`, reopen it, and use standalone Play to verify Idle/Walk/Jump restoration.

## Automated verification

`PicoAnimationTests` covers native asset round trips, invalid section links, runtime section jumps, Notify and
NotifyWindow dispatch, root motion, blend-out completion, interruption exactly once, Registry/Manager loading,
and component loading through AnimationSet references.

Debug and Release full builds succeed, and both configurations pass all 16 CTest targets. The focused animation
executable passes all 13 assertions.

## Deliberate limits

- One DefaultSlot only; no layered bone masks, slot groups, sync groups, or concurrent Montage priority.
- No visual Montage authoring timeline. `.pmontage` is currently produced by code/tools and inspected in Preview.
- No animation retargeting, IK, blend spaces, state-machine graph, morph targets, LOD, or PhysicsAsset.
- Material slots use four component overrides in the first version; automatic imported-material conversion remains
  a later asset-pipeline task.
- A real humanoid asset is not committed until its source and license have been selected and recorded.
