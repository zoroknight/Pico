# Month 8.5: Skeletal Asset Preview And Import

This follow-up closes the editor usability gap between Assimp conversion and Pico's runtime animation
path. External files no longer need to be imported to `Saved`, copied by hand into `Content`, assigned
to a temporary map Actor, and matched manually with `/Game` paths.

## User Workflow

1. Start `PicoEditor` and click `Import Skeletal` in Content Browser.
2. Choose a `.gltf`, `.glb`, or `.fbx` file. Assimp converts it in memory and opens `Skeletal Asset
   Preview`; no project file has been created yet.
3. Inspect the reference pose or an imported clip. The preview provides play/pause, restart, speed,
   timeline seeking, clip selection, orbit, zoom, and automatic framing.
4. Set a `/Game` destination and asset name, then click `Import To Project`.
5. Pico writes `.pskeleton`, `.pskeletalmesh`, and uniquely named `.panimation` files under project
   Content, fixes every embedded Skeleton reference, refreshes AssetRegistry, and selects the new mesh.
6. Double-click a registered Skeletal Mesh or Animation Clip to reopen the same preview without
   creating or modifying a map Actor.

## Editor-Only Preview World

```text
Skeletal Asset Preview window
  -> rooted transient PWorld
  -> transient PActor
  -> PSkeletalMeshComponent + PAnimInstance
  -> dedicated FSceneViewportRenderer
```

The preview World is separate from the editor document World. It is never serialized, has no Gameplay
Framework objects, and cannot make the current map dirty. It is explicitly added to Pico's GC RootSet
while open and removed before `DestroyObjectTree`, so a timed collection cannot invalidate editor-held
pointers. This is the small Pico equivalent of UE's Persona Preview Scene and debug skeletal component.

The preview uses the normal runtime component, pose sampler, CPU skinning, AssetManager, and renderer.
It therefore validates production boundaries instead of maintaining a second editor-only animation
implementation. `PAnimInstance::SetPlaybackTime` and
`PSkeletalMeshComponent::SetAnimationPlaybackTime` provide deterministic timeline seeking and rebuild
the skinned render data immediately.

## Import Ownership

Quick Preview owns an in-memory `FSkeletalImportResult`. `Import To Project` derives physical Content
paths from the chosen virtual folder, rewrites mesh and clip Skeleton references to the final
`.pskeleton`, rejects collisions before writing, removes partial output on failure, rescans
AssetRegistry, invalidates AssetManager entries, and opens the registered result.

`PicoAssetTool import-skeletal` remains available as the headless conversion layer for tests, build
automation, and future AI Agent tools. The editor workflow is an orchestration layer over the same
`PicoAssetImport` adapter rather than a separate importer.

## Deliberate Limits

- Skeletal reimport metadata and source dependency copying are not yet implemented.
- Preview does not yet draw the bone hierarchy, sockets, normals, PhysicsAsset shapes, or Root Motion
  trails.
- Material extraction from glTF/FBX remains separate; the preview uses the component's assigned
  material or Pico's renderer fallback.
- Animation Blueprint, Montage, blend graphs, retargeting, and GPU skinning remain later milestones.

## Verification

- `PicoAnimationTests` verifies AnimInstance and component timeline seeking plus skinned-data rebuild.
- `PicoAssetImportTests` verifies real Assimp glTF and FBX conversion when local source fixtures exist.
- `PicoEditor -frames=5` verifies normal editor initialization and shutdown with the new module linked.
