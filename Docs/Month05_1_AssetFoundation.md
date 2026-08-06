# Month 05.1: Asset Foundation

This milestone establishes asset identity and discovery without introducing importers, resource
loading, or Content Browser UI. The goal is to give those later systems one small, deterministic
contract to build on.

## Ownership

```text
PicoEngine -> PicoAsset -> PicoCore
          \-> PicoObject -> PicoCore
```

- `PicoCore` owns `FAssetPath`, because reflection and serialization need an asset-reference value
  type without depending on the registry.
- `PicoAsset` owns file discovery and metadata. It does not know about `PObject`, rendering, or UI.
- `FEngineLoop` owns the active `FAssetRegistry`, scans it during `Init`, exposes it to higher layers,
  and clears it during `Exit`.
- `PicoObject` can reflect and serialize `FAssetPath`, but it does not resolve or load assets.

This is intentionally smaller than Unreal's Asset Registry. It preserves the useful separation
between asset identity, metadata discovery, and loading while avoiding package databases and
background gatherers before Pico needs them.

## Virtual Paths

`FAssetPath` is a validated project-relative identifier:

```text
/Game/Maps/EditorWorld.pworld
/Game/Models/Robot.pmesh
/Game/Textures/Grid.ptex
/Game/Materials/Metal.pmat
```

Rules:

- Paths must start with `/Game/`.
- Backslashes are normalized to forward slashes.
- Empty, `.` and `..` segments are rejected.
- Windows-invalid filename characters and control characters are rejected.
- A native file extension is required.
- An invalid path is represented by an empty `FAssetPath`; arbitrary disk paths cannot be stored as
  asset references.

Unlike UE long package names, Pico's first format keeps the native extension in the virtual path.
That makes disk mapping explicit and keeps the first registry easy to inspect. This can be revised
behind `FAssetPath` later without exposing raw filesystem paths to scene data.

## Registry Scan

`FAssetRegistry::ScanProjectContent` recursively scans the active project's `Content` directory and
recognizes four native formats:

| Extension | Type |
| --- | --- |
| `.pworld` | `World` |
| `.pmesh` | `StaticMesh` |
| `.ptex` | `Texture` |
| `.pmat` | `Material` |

Source files such as `.obj` are ignored until the importer milestone. Records contain virtual path,
canonical disk path, type, size, and last-write time. Results are sorted by virtual path so scans and
tests are deterministic.

Lookup and collision detection are case-insensitive, matching the maintained Windows target. If two
files map to the same case-folded virtual path, neither ambiguous record is registered and both are
reported as issues. Refresh builds a complete candidate set before replacing the live arrays, so
lookups never observe a half-updated registry.

A project with no `Content` directory is a valid empty project. A root access failure reports failure
and preserves the previous registry; engine-only mode clears project records.

## Reflection And Persistence

`EPropertyType::AssetPath` is supported by property metadata, generic capture/apply code, diagnostic
dumps, and the current inspector UI. The UI is read-only for this type until the Content Browser can
provide validated assignment.

Persistence versions are now:

| Format | Current | Read compatibility | AssetPath allowed |
| --- | ---: | --- | --- |
| `.pobj` | 3 | 1, 2, 3 | version 3 |
| `.pworld` | 2 | 1, 2 | version 2 |

Readers gate property types by archive version. This prevents a new `AssetPath` payload from being
mislabelled as an older archive while retaining compatibility with the repository's existing v1
`EditorWorld.pworld` scene.

## Current Boundary

This milestone discovers metadata; it does not load GPU resources or import source files. The next
layers should use these boundaries:

```text
Content Browser -> FAssetRegistry (query metadata)
Importer        -> project Content (produce native files) -> registry refresh
Resource Loader -> FAssetPath -> FAssetRegistry -> typed runtime resource
Scene property  -> FAssetPath only
```

Keeping scene references virtual is what will later make project relocation, packaging, dependency
collection, and AI-authored commands possible without embedding machine-specific paths.

## Verification

Automated coverage includes path validation and normalization, deterministic scans, ignored source
files, metadata lookup, case-insensitive queries, stale-record removal, reflection access, object and
World round trips, and v1/v2 archive compatibility. Both Debug and Release configurations are part
of the milestone acceptance.
