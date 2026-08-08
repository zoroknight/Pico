# Month 3.15: Native Delegates And Weak Object Binding

## Goal

This milestone adds the native event layer required by Gameplay, physics, animation, networking,
and future AbilityTasks. It follows Unreal Engine's separation between type-safe native delegates
and reflection-backed dynamic delegates. Only the native layer is implemented here; reflected
invocation remains a later `PFunction` milestone.

## Module Boundaries

The implementation is split into three layers:

```text
PicoCore
  FDelegateHandle
  TDelegate<Signature>
  TMulticastDelegate<void(Args...)>

PicoObject
  TObjectDelegate<Signature>
  TObjectMulticastDelegate<void(Args...)>
  generation-safe FObjectHandle guards

PicoEngine
  PWorld::OnActorSpawned
  PActor::OnDestroyed
```

`PicoCore` has no dependency on `PObject`. The Object wrappers add member-function binding without
making the generic delegate container aware of the object registry.

## Native API

Single-cast delegates support:

- `BindStatic` and `BindLambda`;
- `IsBound`, `IsBoundTo`, and `Unbind`;
- `Execute` and `ExecuteIfBound`;
- return values for non-void signatures.

Multicast delegates support:

- `AddStatic` and `AddLambda`;
- stable `FDelegateHandle` identities;
- `Remove`, `RemoveAll`, and `Clear`;
- `IsBound`, `IsBoundTo`, `Num`, and `Broadcast`.

Multicast signatures are restricted to `void` return values because the engine does not define an
implicit result-combination policy for multiple listeners.

## Broadcast Semantics

Each broadcast snapshots the handles that are active at its start. Before every invocation, the
live binding is looked up again. This establishes deterministic game-thread behavior:

- listeners run in insertion order;
- a listener removed before its turn is skipped immediately;
- self-removal is safe;
- `Clear` during broadcast prevents remaining callbacks;
- listeners added during a broadcast start with the next broadcast;
- nested broadcasts receive their own snapshot;
- an exception restores broadcast depth and compacts inactive bindings before propagating.

The delegate container is not thread-safe. Cross-thread dispatch, task queues, and synchronization
are outside this milestone.

## Weak PObject Binding

`AddObject` and `BindObject` store the target's `FObjectHandle` and a typed member-function pointer.
The guard resolves the handle before invocation and rejects missing or beginning-destroy objects.
The callback resolves the handle again instead of retaining a raw target pointer.

This gives weak binding the same generation safety as other Pico object references:

```text
bind object at slot 4, serial 12
  -> destroy object
  -> slot 4 reused with serial 13
  -> old binding still resolves to null
```

CDOs and default-subobject templates cannot be weak delegate targets because they have no runtime
handle. Full strong/weak reflected object properties remain part of the later GC milestone.

## Engine Lifecycle Events

`PWorld::OnActorSpawned` broadcasts after the Actor has been added to its Level and, when the World
has begun play, after `BeginPlay`. A listener therefore observes a complete registered Actor.

`PActor::OnDestroyed` broadcasts once after pending-destroy and EndPlay state has been committed.
The normal World destruction path still has live component objects at this point. `BeginDestroy`
contains an idempotent fallback so World teardown and direct object-tree destruction also emit the
event.

Engine lifecycle events catch and log listener exceptions. Core lifecycle state is changed before
broadcast and cannot depend on listener success.

## Deliberate Limits

This milestone does not implement:

- dynamic or serialized delegates;
- reflected function-name binding;
- Blueprint assignment;
- payload parameters distinct from the declared signature;
- shared-pointer weak binding;
- thread-safe multicast mutation;
- a global event bus.

Dynamic delegates should be added only after `PFunction` provides validated reflected invocation.
Existing editor `std::function` command callbacks remain appropriate where ownership is one-to-one;
they are not mechanically replaced by multicast events.

## Acceptance Coverage

Tests verify:

- static, lambda, void, and value-returning single-cast delegates;
- stable handles and idempotent removal;
- insertion order, self-removal, remove-before-turn, and add-during-broadcast;
- clear and nested broadcast behavior;
- exception cleanup;
- mutable and const PObject member binding;
- automatic expiry after destruction;
- protection against object-slot reuse;
- `RemoveAll` by object;
- Actor spawn and destroy lifecycle ordering;
- listener destruction and lifecycle-listener exception isolation.
