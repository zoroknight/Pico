# Month 03.6 Actor Transform

This milestone exposes an Actor transform without storing a second transform on `PActor`.

## Transform Ownership

The root scene component is the spatial representation of an Actor:

```text
PActor
  -> RootComponentHandle
    -> PSceneComponent
      -> RelativeTransform
      -> WorldTransform
```

Actor transform accessors forward to the root component:

```cpp
Actor->GetActorTransform();
Actor->SetActorLocation(FVector3(100.0f, 0.0f, 50.0f));
```

An Actor without a valid root component returns `FTransform::Identity`. Transform setters return
`false` because there is no spatial component to update.

## World and Relative Space

`PSceneComponent` now exposes both:

```cpp
GetWorldTransform();
SetWorldTransform(Transform);
```

There is no component attachment tree yet, so world space and relative space are currently the
same. Keeping the world-space setter as a separate API preserves the contract needed by `PActor`.
When parent attachment is added, `PSceneComponent::SetWorldTransform` can convert the supplied
world transform into a transform relative to its parent.

## Root Creation Policy

`PActor` does not create a root component automatically. A caller or concrete Actor type creates
and assigns one explicitly:

```cpp
PSceneComponent* Root =
    Actor->CreateComponent<PSceneComponent>("RootComponent");
Actor->SetRootComponent(Root);
```

Automatic default components are deferred until Pico has a constructor/default-subobject model.

## Handle Safety

`RootComponentHandle` is resolved through the object registry each time it is used. If the root
component is destroyed, the stale handle resolves to `nullptr`; Actor transform getters then return
identity and setters fail without dereferencing released memory.
