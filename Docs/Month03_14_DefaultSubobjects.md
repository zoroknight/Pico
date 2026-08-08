# Month 3.14: Default Subobject Templates

## Goal

This milestone extends Pico's Class Default Object path with a compact version of Unreal Engine's
native default-subobject model. A native class can now declare a stable object graph once, store
that graph with its `PClass`, and materialize an independent graph for every runtime instance.

The implementation intentionally covers the semantics needed by future Character, Camera, and
Movement classes without copying Blueprint SCS, the complete Archetype chain, or every
`FObjectInitializer` override feature from UE.

## Class Template Graph

Each `PClass` owns ordered `FDefaultSubobjectRecord` entries. A record stores:

- the stable subobject name and exact reflected class;
- the class-owned template object;
- root identity;
- attachment parent name and optional socket name.

Template objects are outered to the owning class CDO. They have `Transient`, `DefaultSubobject`,
and `RootSet` flags, but are not inserted into `FObjectRegistry` and therefore have no runtime
handle.

```text
PClass
  -> CDO
  -> DefaultSubobject records
       -> template object
       -> root / parent / socket metadata
```

When a derived class is registered, Pico clones the superclass records and template values into
the derived class before calling its virtual `DefineDefaultSubobjects`. Requesting an inherited
name with the same class returns the derived clone; requesting that name with a different class
fails. This provides a small, deterministic inheritance model without implementing UE's complete
subobject override machinery.

## Declaration API

Native classes override `DefineDefaultSubobjects(FObjectInitializer&)` and use:

- `CreateDefaultSubobject<T>(Name)`;
- `SetRootSubobject(Subobject)`;
- `AttachSubobject(Subobject, Parent, SocketName)`.

`PSandboxPawn` now declares `DefaultSceneRoot` and `SandboxPlayerMesh` through this path. The mesh
and material defaults live on the mesh template. `FSandboxGameInstance` only spawns the Pawn and
binds runtime input; it no longer assembles the Pawn's component structure.

## Instance Construction

The unified `NewObject` path now runs in this order:

```text
native constructor
  -> copy reflected values from the selected object template
  -> register the root object with PostInit deferred
  -> create and register all default-subobject instances
  -> let the owner adopt subobjects
  -> restore root and attachment relationships
  -> dispatch PostInitProperties
```

Default-subobject instances copy reflected values from their class-owned templates. Existing
instances remain isolated when a template or sibling instance changes. Failed graph creation
rolls back the partially constructed object tree.

`PActor` adopts component-derived default subobjects into its component handle list and resolves
their class-declared root and attachment metadata through the same component APIs used by normal
runtime components.

## Persistence And Editor Rules

Materialized default subobjects remain ordinary members of the runtime World graph, so `.pworld`
capture records their reflected overrides and relationships. During load, Actor construction first
materializes the implicit graph. The loader then matches serialized component records by stable
name and exact class, reuses those instances, and applies saved values. It does not create a second
copy.

An individual default subobject cannot be renamed or deleted. The rule is enforced in the object,
Actor, editor-command, and Outliner layers. Destroying the owning Actor or World still destroys the
complete runtime tree normally.

## UE Correspondence And Deliberate Limits

The learning correspondence is:

```text
UE native constructor + CreateDefaultSubobject
  ~= Pico DefineDefaultSubobjects + FObjectInitializer

UE CDO-owned component templates
  ~= Pico PClass default-subobject records and template objects

UE instance graph initialization
  ~= Pico NewObject materialization and owner relation hooks
```

Not implemented in this milestone:

- Blueprint Simple Construction Script nodes;
- arbitrary Archetype chains;
- `SetDefaultSubobjectClass`, suppression, or optional subobject APIs;
- editor class-default editing and propagation to existing instances;
- instanced reflected object-reference properties.

## Acceptance Coverage

Tests verify:

- base and derived class template graphs;
- derived ownership of inherited template clones;
- stable root and attachment metadata;
- registry isolation and template flags;
- reflected template values and instance isolation;
- runtime root/component adoption;
- rename and delete protection in runtime and editor commands;
- deterministic World capture and load without duplicate components;
- saved per-instance overrides after reconstruction;
- the real `PSandboxPawn` component graph and input workflow.
