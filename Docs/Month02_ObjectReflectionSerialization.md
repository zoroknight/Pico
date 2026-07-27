# Pico Month 02: Object, Reflection, And Serialization

Month 02 builds the runtime metadata foundation used later by serialization, garbage collection, editor property panels, and replication. The work is split into small stages so each layer has a concrete consumer and test boundary.

## Stage 2.1: Object And Type Foundation

Stage 2.1 establishes this dependency direction:

```text
PicoLaunch
  -> PicoEngine
    -> PicoObject
      -> PicoCore
```

`PicoCore` now provides `FName`. `PicoObject` owns managed object identity, class metadata, class registration, object registration, and object construction. It does not depend on `PicoEngine`.

### FName

`FName` interns text in a process-wide pool. Equal text shares a comparison index, making equality and hashing independent of string length.

The comparison index is process-local and must never be written to an asset, save file, or network packet. Persistent formats serialize the source text and rebuild an `FName` when loaded.

The first implementation is case-sensitive and reserves both an empty string and `None` for the zero entry. Numbered name suffixes are intentionally deferred.

### PClass

`PClass` is static runtime metadata with:

- a class name
- a superclass pointer
- the native object size
- a constructor function
- a process-local native C++ type token

Classes register explicitly with `FClassRegistry`. A superclass must already be registered, which makes initialization order deterministic and testable.

Unlike Unreal's `UClass`, Pico's first `PClass` is not a managed `PObject`. This avoids metaclass bootstrapping, class default objects, archetypes, and generated registration while the core runtime contract is still being learned.

### PObject

Every managed object stores:

- its exact `PClass`
- its `Outer`
- an `FName`
- object flags
- an `{Index, Serial}` runtime handle

`FObjectConstructionParams` supplies class, outer, name, and flags to the base constructor. These values are therefore available while a derived constructor is running. The name `FObjectInitializer` is intentionally reserved for a future property/CDO initialization stage closer to Unreal Engine's semantics.

`Outer` defines naming and containment, not C++ memory ownership. `FObjectRegistry` owns object memory. A live outer cannot be destroyed while child objects still reference it.

`PObject` allocation and deallocation operators are protected. Registered class constructor functions create `FObjectPtr` values, while callers use `NewObject` and `DestroyObject`.

### Object Construction

The current construction path is:

```text
NewObject
  -> validate object system, class, outer, and name
  -> PClass::ConstructObject
  -> PObject constructor receives FObjectConstructionParams
  -> FObjectRegistry assigns an FObjectHandle and takes ownership
  -> PObject::PostInitProperties
  -> return the registered object
```

If `PostInitProperties` throws, the registry destroys the new object's child tree and then the object itself before propagating the exception. No half-registered object remains.

Object handles are weak runtime identities. Destroying an object invalidates its handle. Reusing the same registry slot assigns a new serial, so a stale handle cannot resolve to the replacement object.

### Engine Lifecycle

`FEngineLoop::Init` initializes `PObjectSystem` before future engine runtime systems. `FEngineLoop::Exit` shuts it down in reverse order. The Month 01 unified exit path also performs this cleanup after later initialization failures or exceptions.

The object system currently assumes registration, creation, and destruction occur on the main thread. Thread-safe object registries are outside the current learning scope.

## Stage 2.1 Tests

`PicoCoreTests` verifies `FName` identity, text recovery, None behavior, and hashing.

`PicoObjectTests` verifies:

- intrinsic and derived class registration
- superclass ordering and duplicate class rejection
- template and metadata-driven object creation
- constructor-time identity and `PostInitProperties`
- `IsA`, flags, outer paths, and object lookup
- duplicate object name rejection
- child-before-outer destruction
- stale handle invalidation and slot reuse
- transactional rollback after `PostInitProperties` failure
- shutdown cleanup and object-system reinitialization

`PicoEngineTests` verifies that `GuardedMain` shuts down the object system without changing the Month 01 frame-loop behavior.

## Stage 2.2: Property Reflection

`PProperty` adds the first reflected member metadata to `PClass`. Each property records:

- its `FName`
- its `EPropertyType`
- its native size
- the `PClass` that declares it
- typed member access functions generated from a C++ member pointer

The first supported value types are deliberately limited to `int32`, `float`, and `bool`. `PClass::FindProperty` searches the current class first and then follows `SuperClass`, while `GetProperties` returns only the properties declared directly by that class.

Property values are accessed through type-checked `GetValue`, `SetValue`, and `GetValuePtr` operations. Access fails when the requested C++ type does not match, the object is not an instance of the declaring class, or the generated member accessor is invalid.

Registration is explicit and uses `PProperty::Create<&TObject::Member>`. A class adds properties as one transaction, so an invalid member cannot leave a partially registered schema. Class registration finalizes metadata and rejects invalid classes. Property storage keeps returned `PProperty` addresses stable. Reflection macros and generated registration remain deferred until the manual metadata path has a serialization consumer.

## Stage 2.3: Serialization

`FArchive` separates byte storage from object reflection. The first concrete archives are:

- `FMemoryWriter`, which appends serialized bytes to a byte vector
- `FMemoryReader`, which reads from an existing byte span and detects truncated data

The archive writes fixed-width integers and floats in little-endian order. Booleans use one byte, and strings use a bounded length followed by their bytes. This makes the format independent of native struct padding and prevents a malformed file from requesting an unlimited string allocation.

`SaveObject` walks the object's class hierarchy from base class to most-derived class. For every reflected property it writes:

- the property name as text
- the reflected property type
- the property value

The object header contains a magic number, format version, class name, object name, flags, and property count. Class, object, and property names are serialized as text. Process-local `FName` comparison indices never enter the persistent format.

`LoadObject` first reads the complete serialized record, resolves the saved class name through `FClassRegistry`, creates the instance through `NewObject`, and applies values through `PProperty::SetValue`. Unknown property names are ignored so a removed property does not invalidate an older file. A known property with a changed type is rejected. Malformed data and failed property application leave no half-loaded object.

`EObjectSerializationError` reports why an operation failed instead of reducing every failure to `false` or `nullptr`. It distinguishes invalid archives, unsupported versions, missing classes, object name collisions, property type changes, `PostLoad` failures, file I/O failures, oversized files, and unexpected trailing data.

`SaveObjectToFile` writes a complete temporary file before replacing the destination. On platforms where a rename cannot replace an existing file directly, the old file is kept as a short-lived backup until the replacement succeeds. A failed write therefore does not truncate the previous valid object file.

`SaveObjectToFile` and `LoadObjectFromFile` provide the first persistent `.pobj` round trip. The current stage serializes one object at a time. `Outer` is supplied by the loading caller rather than stored, so object graphs and object-reference fixups remain a later stage.

The current load order is:

```text
read and validate archive
  -> find PClass by saved text name
  -> NewObject (constructor and PostInitProperties run)
  -> apply serialized reflected properties
  -> PObject::PostLoad
  -> return loaded object
```

`PostLoad` runs only after every serialized property has been applied. It is the place for an object to rebuild cached state that depends on loaded values. If property application or `PostLoad` fails, `FObjectRegistry::DestroyObjectTree` removes the new object and any children created during initialization or loading.

The serialization tests cover memory and file round trips, inherited properties, replacement of an existing file, `PostLoad` ordering, removed properties, changed property types, unsupported versions, missing classes, duplicate object names, truncated archives, trailing file data, and transactional rollback after `PostLoad` failure.

## Deferred Work

Stage 2.1 intentionally does not implement:

- function reflection
- a header parser or generated reflection code
- garbage collection or weak pointer wrappers
- class default objects and archetypes
- Actor, Component, RPC, or replication
- object graph serialization and object-reference fixups
- schema migration beyond ignoring removed properties and rejecting changed property types
