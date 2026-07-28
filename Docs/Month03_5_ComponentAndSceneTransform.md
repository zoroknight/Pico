# Month 03.5 Component and Scene Transform

This milestone connects the existing Core `FTransform` type to the runtime Actor model through components.

## Goals

- Add `PActorComponent` as the base type for Actor-owned components.
- Add `PSceneComponent` as the first spatial component.
- Let `PActor` create and enumerate owned components.
- Let `PActor` store a root scene component.
- Register components when an Actor begins play.
- Unregister components when an Actor ends play or is destroyed.

## Object Shape

Components are regular `PObject` instances owned by the object registry.

The runtime object path is:

```text
GameWorld.PersistentLevel.CubeActor.RootComponent
```

The Outer chain is:

```text
RootComponent.Outer = CubeActor
CubeActor.Outer = PersistentLevel
PersistentLevel.Outer = GameWorld
```

`Outer` gives the component a name scope and a destruction parent. `PActor::ComponentHandles` gives the Actor a direct list of components it owns.

Both relationships are needed:

- `Outer` connects the component to the object tree.
- `ComponentHandles` lets gameplay code and engine code enumerate Actor components without scanning the whole registry.

## New Runtime Types

```text
PObject
  -> PActor
  -> PActorComponent
      -> PSceneComponent
```

`PActorComponent` currently provides:

- `GetOwner`
- `GetWorld`
- `IsRegistered`
- `RegisterComponent`
- `UnregisterComponent`
- `OnRegister`
- `OnUnregister`

`PSceneComponent` currently provides:

- `RelativeTransform`
- relative location, rotation, and scale accessors
- `GetWorldTransform`

There is no attachment tree yet, so `GetWorldTransform` returns `RelativeTransform`.

## Actor Integration

Actors now store:

```cpp
std::vector<FObjectHandle> ComponentHandles;
FObjectHandle RootComponentHandle;
```

Components are created through:

```cpp
PSceneComponent* Root = Actor->CreateComponent<PSceneComponent>("RootComponent");
Actor->SetRootComponent(Root);
```

Internally this uses:

```cpp
NewObject(ComponentClass, Actor, Name);
```

That keeps registry ownership, reflection, object paths, and destruction behavior consistent with Actors and Levels.

## Lifecycle

Actor BeginPlay:

```text
PWorld::Tick
  -> PActor::DispatchBeginPlay
    -> RegisterAllComponents
    -> Actor BeginPlay override
```

Actor EndPlay:

```text
PWorld::DestroyActor / PWorld::TearDown
  -> PActor::DispatchEndPlay
    -> Actor EndPlay override
    -> UnregisterAllComponents
```

Actor destruction:

```text
DestroyObjectTree(Actor)
  -> destroy child components first
  -> destroy Actor
```

Because components are outered to their Actor, destroying the Actor tree invalidates component handles too.

## Deferred Work

- Component tick
- SceneComponent attachment parent and children
- Primitive/Mesh component
- Scene proxy and renderer registration
- Editor object tree for World/Level/Actor/Component
- Full component serialization policy
