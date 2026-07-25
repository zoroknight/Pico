# Pico Month 02.9: Runtime Hardening

Month 02.9 prepares the object and reflection runtime for World, Actor, and Component ownership.

## Type-Safe Property Access

Reflected properties are declared with a C++ member pointer:

```cpp
PProperty::Create<&PDemoCharacter::Health>(FName("Health"))
```

`PProperty` stores generated mutable and const access functions. It no longer computes an address from `offsetof`, which is only conditionally supported for polymorphic, non-standard-layout object types.

`PClass::Create<TObject>` and `PProperty::Create<&TObject::Member>` also carry the same process-local native type token. A property created from another C++ owner type is rejected before it can enter the class schema.

Runtime access still validates:

- the requested reflected type
- the native value size
- the property's declaring `PClass`
- the object's class hierarchy

## Transactional Class Metadata

`PClass::AddProperties` validates a complete property batch before changing the class. Duplicate names, inherited-name collisions, unsupported types, and invalid accessors reject the entire batch.

`FClassRegistry::RegisterClass` rejects invalid metadata and finalizes a valid class. Finalized metadata is immutable, so property addresses and serialized schemas cannot change while instances are alive.

## BeginDestroy

`PObject::BeginDestroy` is called exactly once before native destruction.

During `BeginDestroy`:

- the object is still registered
- its handle still resolves
- `IsBeginningDestroy` returns true
- new child objects cannot use it as their `Outer`

Object trees run `BeginDestroy` from children to parents. This gives future Components a deterministic place to unregister from their Actor while the Actor is still alive.

Exceptions from `BeginDestroy` are logged and swallowed so registry shutdown can continue.

## Verification

`PicoObjectTests` verifies:

- member-pointer property reads and writes
- atomic rollback of an invalid property batch
- rejection of a member pointer from the wrong native owner type
- invalid class registration rejection
- metadata finalization and late-mutation rejection
- child-before-parent `BeginDestroy`
- handle visibility during `BeginDestroy`
- rejection of child creation during destruction
- handle invalidation after object-tree destruction
