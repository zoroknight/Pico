# Month 04.1 World Asset Data

This milestone defines the first version of Pico's scene asset data without
creating objects while loading. It establishes the persistent boundary used by
the next milestone's transactional World reconstruction.

## Scope

The completed flow is:

```text
PWorld
  -> CaptureWorld
  -> FWorldAssetData
  -> FMemoryWriter
  -> .pworld-compatible bytes
  -> FMemoryReader
  -> FWorldAssetData
```

`DeserializeWorldAsset` returns validated plain data. It does not create a
`PWorld`, call `NewObject`, apply properties, or invoke `PostLoad`.

## Scene Graph Records

The format stores a flat list of objects. `OuterId` reconstructs the ownership
hierarchy:

```text
PWorld
  -> PLevel
    -> PActor
      -> PActorComponent
```

Each record contains:

- a file-local 64-bit scene object ID
- an outer scene object ID
- class and object names as text
- object flags
- tagged reflected properties

Scene object ID zero means no reference. These IDs are not `FObjectHandle`
values and do not serialize registry indices or serial numbers.

Relationships that cannot be derived from Outer are stored separately:

- the root component selected by an Actor
- the attach parent selected by a SceneComponent
- the persistent and current levels selected by a World

Runtime lifecycle state, tick state, pending destruction, registration state,
OpenGL resources, editor selection, and ImGui layout are not captured.

## Shared Property Codec

The former private property record in `ObjectSerialization.cpp` now lives in
`SerializedProperty`. Both `.pobj` and World asset data use the same tagged
property codec for:

- `int32`
- `float`
- `bool`
- `FVector3`
- `FRotator`
- `FTransform`

The existing `.pobj` header and property byte layout remain unchanged.

## Binary Format

World asset format version 1 is:

```text
Header
  Magic = PWLD
  Version
  ObjectCount
  RelationCount
  WorldId
  PersistentLevelId
  CurrentLevelId

ObjectRecords
RelationRecords
```

Integers and floats use the little-endian `FArchive` representation. Native C++
struct memory and padding are never written directly.

## Validation

Data must pass validation before it is written and after it is read. Validation
rejects:

- zero or duplicate object IDs
- missing or cyclic Outer relationships
- duplicate names in one Outer
- missing reflected classes
- invalid World, Level, Actor, or Component ownership
- duplicate, invalid, or excessive property records
- invalid Actor root components
- cross-Actor attachments
- attachment cycles
- references to missing objects

Current limits are:

```text
Objects per World:       65,536
Relations per World:    131,072
Properties per object:    4,096
Class/object/property name: 1,024 bytes
```

`FArchive` continues to enforce its one-megabyte general string limit.

## Determinism

`CaptureWorld` assigns scene IDs in stable traversal order:

```text
World
  -> Levels
    -> Actors
      -> Components
```

Reflected properties remain ordered from base class to derived class.
Serializing unchanged `FWorldAssetData` therefore produces identical bytes.

## Follow-up

Transactional runtime reconstruction is implemented by the next milestone,
`Month04_2_WorldReconstruction.md`.

Editor menus, Content Browser integration, Undo/Redo, and clipboard operations
remain outside this milestone.
