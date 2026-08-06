# Month 5, Week 3: Content Browser and Editor Asset Workflow

## Goal

Week 3 turns the static-mesh pipeline from a command-line/runtime feature into an explicit editor
workflow. The editor can discover assets, choose one without depending on Registry order, import an
external OBJ, reimport it later, and apply it to scene components through the transaction system.

## Architecture

The implementation keeps asset files and scene state in separate command domains:

```text
Content Browser (ImGui)
  -> FEditorAssetSelection (stable FAssetPath)
  -> FEditorAssetWorkflowController
       -> FStaticMeshImportDialog / FDeleteAssetsDialog
       -> FEditorAssetService (import, reimport, refresh, cache invalidation)

Details / Content Browser commands
  -> FEditorCommandService
  -> World transaction snapshot
  -> PStaticMeshComponent::StaticMeshAsset
```

`FEditorAssetSelection` stores only `/Game/...` paths. It resolves the current `FAssetRecord` on
demand, so a Registry refresh cannot leave a dangling record pointer. If the asset disappears,
`Validate` clears the selection.

`FEditorAssetService` has no ImGui dependency. Import copies the selected OBJ under
`Content/Source`, writes the native `.pmesh`, and creates a `.pmesh.import` sidecar containing the
project-local source path and import options. Reimport reads this metadata and atomically replaces
the native mesh only after parsing and validation succeed. A successful import invalidates the
specific CPU mesh cache entry and refreshes the Registry; renderer-owned buffers update when they
observe the replacement mesh data.

## Editor Workflow

The dockable Content Browser provides:

- `/Game` folder navigation.
- Text search and asset-type filtering.
- `Ctrl` toggle selection, `Shift` range selection, and filtered `Ctrl+A` selection.
- Name, type, virtual path, size, and modified-time columns.
- `Import OBJ`, `Reimport`, and `Refresh` actions.
- A pre-import settings modal with source statistics, original/final dimensions, automatic and
  explicit unit presets, coordinate conversion, and texture-V control.
- `Import Options` / `Reimport With Options...` for editing persisted settings.
- Static Mesh deletion with reference reporting, optional source removal, and optional scene
  reference cleanup. Week 4 generalizes the same protocol to Texture and Material assets.
- Multi-asset selection with a stable primary asset, double-click Static Mesh creation, context
  commands, and drag/drop.

Static Mesh creation and component addition now require the selected Content Browser asset. The old
"first registered mesh" behavior is removed. Details supports `Use Selected`, `Clear`, and a
`PICO_ASSET_PATH` drag/drop payload. Assignment and clearing call `FEditorCommandService`, including
multi-selected StaticMeshComponents in one Undo/Redo transaction.

## Import Layout

For `/Game/Meshes/Chair.pmesh`, the editor writes:

```text
Content/
  Meshes/Chair.pmesh
  Meshes/Chair.pmesh.import
  Source/Meshes/Chair.obj
```

Absolute source-machine paths are not persisted. The sidecar points to
`/Game/Source/Meshes/Chair.obj`, keeping reimport portable with the project. `Imported` is not used
as a permanent asset category: import origin belongs in sidecar metadata, while the destination
folder describes the resulting asset type.

## Scale and Framing

OBJ has no dependable universal unit metadata. Pico analyzes geometry before import and offers
`Auto`, `Centimeters`, `Meters`, `Millimeters`, `Normalize`, and `Custom` scaling. Auto treats
sub-unit normalized meshes as a request to fit the longest axis to 100 Pico units, treats common
1-10 unit assets as meters, and otherwise preserves source units. The chosen scale is saved in the
sidecar and the final dimensions are visible before import.

Pressing `F` computes the union of selected primitive world bounds, including transformed Cube and
Static Mesh corners, then frames that union. Camera distance, near plane, and movement speed adapt
to the focused radius so tiny authored assets remain inspectable without changing scene scale.

## Asset Deletion

Delete is an editor asset operation, not a World transaction. The editor reports every
`PStaticMeshComponent` referencing the asset and offers two policies: keep references as missing, or
clear all references in one reversible scene transaction before deleting. The generated `.pmesh`
and sidecar are deleted together; the project-local source OBJ is optional. Registry, CPU cache,
asset selection, and renderer GPU resources are invalidated after a successful operation.

Deletion uses an explicit stage/commit/rollback protocol. Asset files are first renamed to temporary
paths and removed from the Registry. Scene references are cleared only after staging succeeds. If
reference cleanup cannot commit, every staged file and Registry record is restored before the
operation reports failure.

The Week 4 closeout extends this protocol to mixed Static Mesh, Texture, and Material selections.
It also reports asset-to-asset dependencies and restores edited Material files when staged Texture
deletion is rolled back.

## Verification

`PicoEditorAssetTests` covers source analysis, native output, source copying, sidecar creation,
Registry refresh, path-based selection, CPU-cache invalidation, option updates, failed-reimport
preservation, reversible single and batch reference cleanup, staged-deletion rollback,
source-preserving deletion, complete and batch deletion, Ctrl toggle selection, Shift range
selection, and selection invalidation. The
complete Debug and Release suites each contain eight CTest targets.

## Deliberate Limits

This milestone supports OBJ to Static Mesh only. It does not add thumbnails or asset rename/move,
FBX or glTF, materials or textures, or async import. Those remain separate
milestones so the first editor asset path stays small and inspectable.
