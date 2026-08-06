# Month 5, Week 4: Texture, Material, and Minimal PBR

## Goal

Week 4 completes the first asset-driven rendering vertical slice. Source images become native Pico
textures, materials reference those textures, scene components reference materials, and the OpenGL
viewport evaluates a small Cook-Torrance PBR model.

## Asset Flow

```text
PNG / JPG / TGA / BMP
  -> stb_image
  -> validated RGBA8 FTextureData
  -> deterministic .ptex v1
  -> FAssetManager CPU cache
  -> renderer-owned OpenGL texture and mipmaps

FMaterialData
  -> .pmat v1
  -> BaseColor + BaseColorTexture + Metallic + Roughness
  -> PStaticMeshComponent::MaterialAsset
  -> Cook-Torrance shader
```

Source image decoding remains in `PicoAssetImport`. Runtime programs load only `.ptex` and `.pmat`
through `PicoAsset`, so packaging does not need image import code.

## Native Formats

`.ptex v1` stores width, height, RGBA8 format identity, byte count, and tightly packed pixels. It
rejects zero dimensions, dimensions above 16384, mismatched byte counts, unsupported versions, and
trailing data.

`.pmat v1` stores linear BaseColor, Metallic, Roughness, and an optional stable `/Game/...ptex`
reference. Parameters must be finite and within the supported PBR ranges. Both formats use temporary
files and backup restoration when replacing an existing asset.

## Scene and Editor

`PStaticMeshComponent` reflects and serializes both `StaticMeshAsset` and `MaterialAsset`. Material
assignment to one or more selected StaticMeshComponents is one editor transaction and supports
Undo/Redo.

The Content Browser provides `Import Texture` and `Create Material`. Textures retain project-local
source metadata for Reimport. Double-clicking a Material opens BaseColor, Metallic, Roughness, and
BaseColorTexture controls. Materials can be assigned from the context menu or Details panel.

## Property-Driven Asset References

The editor now uses a small UE-style property pipeline instead of teaching the Details panel about
individual component classes:

```text
PProperty metadata
  -> Details chooses a widget from property type and asset-reference type
  -> FEditorPropertyService validates and writes the value in a transaction
  -> PObject::PostEditChangeProperty reports the editor-side mutation
  -> reflected serialization persists Serializable, non-Transient properties
  -> FAssetDependencyService discovers and migrates stored references
```

`EPropertyFlags` currently distinguishes `Editable`, `Serializable`, `Transient`, `Replicated`, and
`ReadOnly`. `FPropertyMetadata::AssetReferenceType` constrains an `FAssetPath` to Static Mesh,
Texture, or Material assets. Details therefore only receives an object handle, property name, and
new value; it does not call `PStaticMeshComponent` setters or own scene mutation rules.

Property edits are deferred until all ImGui panels finish drawing. This keeps object handles stable
while controls are active and prevents Undo rollback from replacing the World in the middle of an
ImGui item lifecycle. Asset rename and deletion use the same dependency service, so reference
discovery is no longer duplicated per asset type.

This is intentionally smaller than UE's `FProperty`, `FPropertyChangedEvent`, Asset Registry, and
redirector stack. Tagged property serialization, property redirects, soft-object redirectors, and
container property descriptors remain later milestones.

## Renderer

The OpenGL 3.3 renderer uploads Static Mesh UVs and owns texture objects separately from CPU data.
It refreshes a GPU texture when `FAssetManager` returns replacement source data and generates mipmaps
after upload. Missing materials or textures fall back to the primitive color and untextured shading.

The shader implements GGX distribution, Smith geometry, Schlick Fresnel, one directional light,
simple ambient light, Reinhard tone mapping, and gamma output. This is a compact educational PBR
path, not a physically complete renderer.

## Verification

- `PicoAssetTests` covers `.ptex` and `.pmat` validation and round trips.
- `PicoAssetImportTests` covers image decoding and native Texture output.
- `PicoEditorAssetTests` covers Texture import, Material creation and cache refresh, MaterialAsset
  assignment through the generic property service, dependency discovery, mixed asset deletion,
  reference-cleanup rollback, and Undo/Redo.
- `PicoObjectTests` covers asset-reference metadata and verifies that Transient properties are not
  serialized.
- Debug and Release run the complete eight-target CTest suite.

## Deliberate Limits

- One Material override per StaticMeshComponent.
- One BaseColor Texture per Material.
- No Normal Map because `.pmesh v1` does not contain tangents.
- No material graph, IBL, shadows, texture compression, streaming, or authored mip chains.
- No thumbnail generation, asset redirectors, or folder move operation.
