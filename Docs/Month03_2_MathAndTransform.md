# Pico Month 03.2: Math and Transform

Month 03.2 establishes the spatial types used by Actor, SceneComponent, rendering, physics, and transform replication.

## Module Boundary

The math library lives in `PicoCore` and does not depend on the object or reflection systems:

```text
PicoCore Math
    ^
PicoObject Reflection
    ^
PicoEngine / Projects / Editor
```

Public types are available under `Pico/Core/Math`:

```text
FVector3
FRotator
FQuat
FMatrix4
FTransform
```

`Math.h` is the convenience umbrella header.

## Spatial Conventions

- `+X` is forward.
- `+Y` is right.
- `+Z` is up.
- `FRotator` stores degrees.
- Pitch rotates around Y, Yaw around Z, and Roll around X.
- Runtime rotation and transform composition use normalized quaternions.
- Matrices use row-major storage and column-vector multiplication.
- A transform matrix is `Translation * Rotation * Scale`.

Renderer backends must adapt matrix memory layout at their API boundary instead of changing Core math conventions.

## Transform Composition

Pico follows the UE-style expression:

```cpp
FTransform World = Local * Parent;
```

This applies `Local` first and `Parent` second. Translation composition is:

```text
WorldTranslation =
    ParentRotation(ParentScale * LocalTranslation)
    + ParentTranslation
```

`GetRelativeTransform` recovers Local from World and Parent for representable translation, quaternion, and scale combinations.

Combining non-uniform scale with rotation can mathematically produce shear. `FTransform` does not represent shear, so those compositions are approximate. `FMatrix4` remains available when exact matrix composition is required.

## Reflection

`EPropertyType` now supports:

```text
Int32
Float
Bool
Vector3
Rotator
Transform
```

Math properties use the existing member-pointer authoring path:

```cpp
Pico::FTransform Transform;

PICO_ADD_PROPERTY(Properties, Transform);
```

No special transform macro or `PStruct` system is required. `Vector3`, `Rotator`, and `Transform` are fixed built-in value types for this phase. Arbitrary nested structure reflection remains deferred.

## Serialization

New `.pobj` files use format version 2. The reader accepts:

- version 1 scalar archives
- version 2 scalar and math-property archives

Math values are serialized component by component:

```text
Vector3   = X, Y, Z
Rotator   = Pitch, Yaw, Roll
Transform = Translation, Quaternion, Scale
```

The serializer never writes raw structure memory, avoiding padding and compiler-layout dependencies. Loaded transform quaternions are normalized before being applied.

## Editor

Both `PicoInspector` and the reflected Details implementation now used by `PicoEditor` provide:

- three-axis controls for `FVector3`
- Pitch/Yaw/Roll controls for `FRotator`
- expanded Location/Rotation/Scale controls for `FTransform`

Editing still goes through `PProperty::SetValue`; the UI does not bypass reflection.

## Verification

`PicoCoreTests` covers:

- vector length, normalization, dot, and cross
- quaternion rotation and invalid-quaternion fallback
- Rotator/Quat conversion
- position and inverse-position transforms
- UE-style local/parent composition
- relative transforms
- matrix/transform agreement
- zero-scale protection

`PicoObjectTests` verifies version 1 archive compatibility. `PicoSandboxTests` verifies reflected Vector3, Rotator, and Transform metadata and a version 2 save/destroy/load/PostLoad round trip.
