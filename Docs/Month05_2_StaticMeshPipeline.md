# Month 05.2: Static Mesh Pipeline

This milestone turns the asset registry into a visible asset-driven rendering path. It keeps source
import, native runtime data, scene references, and GPU resources as separate ownership layers.

## End-To-End Flow

```text
OBJ source
  -> PicoAssetImport / TinyObjLoader
  -> validated FStaticMeshData
  -> deterministic .pmesh v1
  -> FAssetRegistry metadata
  -> FAssetManager CPU cache
  -> PStaticMeshComponent FAssetPath
  -> PicoRender GPU cache
  -> OpenGL draw, picking, and selection highlight
```

`PicoAssetImport` is a Developer module. Runtime programs can read `.pmesh` without linking the OBJ
parser, and packaged games will not need source-import code.

## Native Mesh Format

`.pmesh v1` stores:

- Position, normal, and one UV channel per vertex.
- A 32-bit triangle index buffer.
- Contiguous sections with a future material slot name.
- Axis-aligned minimum and maximum bounds.

The reader validates magic, version, count limits, finite floating-point data, triangle counts,
index ranges, contiguous sections, bounds, truncation, and trailing bytes. Saving writes a temporary
file and transactionally replaces an existing destination. Unchanged data produces identical bytes.

Tangents, additional UV channels, collision, LODs, and skeletal data are intentionally outside v1.
They require explicit future format versions instead of silently changing the layout.

## OBJ Import

Pico vendors TinyObjLoader under its MIT license in `ThirdParty/TinyObjLoader`. The importer:

- Triangulates OBJ faces.
- Deterministically reuses equal position/normal/UV index tuples.
- Converts conventional Y-up source data into Pico's Z-up coordinates.
- Optionally flips texture V.
- Generates averaged normals when the source has none.
- Computes bounds and validates the final native mesh before saving.

The reusable API lives in `PicoAssetImport`; `PicoAssetTool` is a thin command-line frontend:

```powershell
.\Build\Debug\PicoAssetTool.exe import-obj source.obj destination.pmesh
```

## Runtime Ownership

`FAssetManager` caches immutable `shared_ptr<const FStaticMeshData>` entries by `FAssetPath`. It
returns the same CPU resource while Registry size and timestamp metadata remain unchanged, then
loads a replacement resource after a registry refresh.

`PStaticMeshComponent` persists only `FAssetPath StaticMeshAsset`. It never serializes disk paths,
CPU pointers, OpenGL handles, or Registry record addresses.

`FSceneViewportRenderer` owns the VAO, vertex buffer, and index buffer cache. A changed CPU resource
replaces the corresponding GPU buffer contents. Static meshes retain component/actor picking,
visibility, color, transforms, and wireframe selection highlighting. Built-in `PCubeComponent`
rendering remains as an independent primitive and regression path.

## Temporary Editor Workflow

Until the Content Browser and asset picker arrive, `Add > Static Mesh` and `Add Component > Static
Mesh Component` use the first registered static mesh in deterministic Registry order. This is only a
test bridge. Week 3 should replace it with an explicit selected-asset argument while continuing to
call `FEditorCommandService`; UI code must not construct scene objects directly.

The PicoSandbox sample includes:

```text
Content/Source/PicoPyramid.obj
Content/Models/PicoPyramid.pmesh
```

## Current Boundary

This milestone does not include Content Browser UI, FBX/glTF, materials, textures, PBR, async IO,
LODs, collision, or skeletal meshes. Its responsibility ends once a validated static mesh can be
imported, referenced persistently, cached, rendered, picked, and transformed.

## Verification

Coverage includes deterministic native bytes, malformed archives, unsupported versions, trailing
data, Registry discovery, CPU cache reuse and refresh, OBJ triangulation, coordinate/UV conversion,
generated normals, component reflection, editor command transactions, and Debug/Release regression.
