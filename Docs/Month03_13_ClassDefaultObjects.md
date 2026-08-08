# Month 3.13: Class Default Objects And Unified Construction

## Goal

This milestone adds the first UE-inspired Class Default Object path without copying the full
Archetype or Blueprint system. It gives reflection, object loading, Actor spawning, and future
editor class defaults one shared initialization baseline.

## Runtime Model

Every successfully registered `PClass` owns one CDO:

```text
PClass
  -> ClassDefaultObject
       native constructor defaults
       reflected class-default values
```

The CDO is constructed after class metadata is finalized. It has `Transient`,
`ClassDefaultObject`, and `RootSet` flags, but it is not inserted into `FObjectRegistry` in this
first version. It therefore has no runtime handle, cannot be saved as a normal object, and cannot
enter a World, Tick, or scene hierarchy.

`GetDefault<T>()` returns a read-only typed pointer. `GetMutableDefault<T>()` exists for tests and
future class-default editing.

## Unified Construction

`NewObject` now accepts `FObjectConstructionParams` as its canonical entry point. Existing typed
and dynamic overloads forward to it.

```text
validate Class, Outer, Name, Flags, Template
  -> call the native construction function
  -> FObjectInitializer copies non-Transient reflected properties
  -> register the object
  -> PostInitProperties
```

The default template is the exact class CDO. A compatible live object may be supplied explicitly
as `FObjectConstructionParams::Template`. Identity fields such as Class, Outer, Name, Flags, and
Handle are not reflected and are never copied from the template.

Pico deliberately performs reflected template copying after the native C++ constructor. This is
smaller than UE's complete constructor/Archetype/subobject pipeline and is sufficient for the
currently supported value properties. The next milestone extends this path with class-owned
default-subobject templates; see
[`Month03_14_DefaultSubobjects.md`](Month03_14_DefaultSubobjects.md).

## Loading

Object loading uses the same `NewObject` path:

```text
native constructor
  -> CDO defaults
  -> serialized property overrides
  -> PostLoad
```

An older file that does not contain a newly added property therefore retains the current CDO
value. `Transient` properties stay at their native constructor value and are neither copied from
the CDO nor restored from an archive.

## Actor Spawn Parameters

`FActorSpawnParameters` provides a structured Actor creation boundary with:

- `Name`
- `OverrideLevel`
- `Owner`
- `ObjectFlags`

Legacy `SpawnActor(Name, Level)` calls remain source-compatible. Owner is stored as a generation-
safe handle, so destroying the owner makes `GetOwner()` resolve to `nullptr` rather than leaving a
dangling pointer.

## Acceptance Coverage

Tests verify:

- one stable CDO per registered class;
- CDO flags, invalid runtime handle, and registry isolation;
- inherited and directly declared defaults on a derived CDO;
- reflected CDO edits affecting new instances;
- instance isolation from its CDO and sibling instances;
- explicit construction templates;
- missing serialized properties retaining CDO values before `PostLoad`;
- CDO serialization rejection;
- Transient properties not copying from CDO;
- structured Actor Spawn Level, Owner, Flags, and normal destruction;
- deterministic child destruction after object-slot reuse.
