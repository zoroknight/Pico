# Month 03.9 Scene Component Attachment

This milestone gives `PSceneComponent` a spatial attachment hierarchy. Attachment is separate from
object ownership:

```text
Object ownership                         Spatial attachment

Actor                                    RootComponent
  -> RootComponent                         -> MeshAnchor
  -> MeshAnchor                               -> Socket
  -> Socket
```

Every component remains outered to and lifetime-owned by its Actor. `AttachParentHandle` and
`AttachChildrenHandles` only describe the transform hierarchy.

## Attachment Rules

Pico supports two initial transform rules:

```cpp
EAttachmentTransformRule::KeepRelative
EAttachmentTransformRule::KeepWorld
```

Attachment is limited to live scene components owned by the same Actor. A component cannot attach
to itself or one of its descendants, and the Actor's root component cannot have an attach parent.
Promoting an attached component to root detaches it while preserving its world transform.

Cross-Actor attachment and UE-style per-axis location, rotation, and scale rules are deferred.

## Hierarchical Transform

World transforms are calculated on demand:

```text
World = Relative * ParentWorld
```

Setting a world transform converts it back to parent-relative space through
`FTransform::GetRelativeTransform`. An unattached component continues to treat its relative
transform as world space.

Pico does not cache component world transforms yet. Dirty propagation can be added when rendering
or large scenes make repeated recursive evaluation measurable.

## Destruction

Attachment does not affect object registry ownership. Destroying an individual scene component:

1. Removes it from its attach parent.
2. Detaches its direct children.
3. Preserves each child's world transform.
4. Continues through the normal component unregister and object destruction path.

Destroying an Actor still destroys all of its components through the Actor's Outer tree, regardless
of their spatial attachment order.

## Editor

The Scene Outliner renders the real component hierarchy recursively. The editor sample starts with:

```text
CubeActor
  -> RootComponent [Root]
    -> ChildComponent
```

Selecting a scene component exposes its attach parent, child count, editable reflected relative
transform, and read-only world transform. `Add Child` creates a new scene component owned by the
same Actor and attaches it to the selected component.

## Deferred Work

- Cross-Actor and socket attachment
- Transform caching and dirty propagation
- Drag-and-drop hierarchy editing
- Attachment reference serialization
- Primitive and static-mesh components
