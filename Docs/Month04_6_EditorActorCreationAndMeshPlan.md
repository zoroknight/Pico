# Month 04.6 Editor Actor Creation and Mesh Plan

This milestone removes the editor-owned startup Cube and adds explicit scene creation through:

```text
Add
  -> Empty Actor
  -> Cube
```

## Current Creation Path

`Empty Actor` creates:

```text
PActor
  -> PSceneComponent "DefaultSceneRoot" [Root]
```

`Cube` creates:

```text
PActor
  -> PCubeComponent "CubeComponent" [Root]
```

The editor assigns unique `Actor_N` and `Cube_N` names, selects the new Actor, and destroys the
whole partially-created Actor if component creation or root assignment fails. Editor startup itself no
longer mutates the new World by injecting sample content.

The runtime `PActor` still permits no root. The editor's Empty Actor factory supplies a
`DefaultSceneRoot` as a convenient transform and attachment anchor, while an Actor with a meaningful
scene component uses that component directly as its root. This keeps the runtime rule separate from
the editor-authored template.

This direct `PCubeComponent` path is intentionally small. It validates dynamic scene creation,
Outliner and Details refresh, transforms, World persistence, and the future create/delete
transaction workflow.

## Planned Unified Mesh Architecture

Cube, Sphere, and imported models should ultimately use one renderable component type:

```text
PActor
  -> PStaticMeshComponent
       -> FAssetReference StaticMesh
       -> material overrides
```

Primitive shapes become built-in mesh assets rather than permanent component classes:

```text
/Engine/BasicMeshes/Cube.pmesh
/Engine/BasicMeshes/Sphere.pmesh
/Game/Models/Tree.pmesh
```

The `.pworld` file stores the component, transform, material overrides, and mesh asset reference. It
does not duplicate mesh vertices or indices. Geometry lives in `.pmesh`; an Asset Manager resolves
and caches it, and the renderer uploads the shared resource to GPU memory. Multiple scene
components can reference the same loaded mesh.

## Actor Factory Direction

Creation should converge on an editor factory boundary:

```text
FEmptyActorFactory
FStaticMeshActorFactory
```

`Add Cube`, `Add Sphere`, and dragging an imported mesh from the Content Browser will all invoke the
same static-mesh factory with different asset references. The current `CreateCubeActor` helper is a
temporary centralized bridge and should be replaced inside that boundary, without changing the
menu command or transaction integration.

## Migration Sequence

1. Add editor transactions to current Empty Actor and Cube creation.
2. Add `.pmesh`, `FAssetReference`, and Asset Manager support.
3. Add `PStaticMeshComponent` and renderer resource caching.
4. Convert built-in Cube and Sphere to engine mesh assets.
5. Route Add-menu primitives and Content Browser drag/drop through `FStaticMeshActorFactory`.
6. Retire `PCubeComponent` after existing `.pworld` compatibility is handled.

Project-authored Actor classes are a separate concern. Once project modules are loaded into
`PicoEditor`, reflected project Actor classes can appear alongside engine factories without moving
project source into the editor.
