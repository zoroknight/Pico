# Pico Month 03.1: Explicit Reflection Macros

Month 03.1 removes repeated native class boilerplate before World, Actor, and Component types are introduced.

## Scope

`ReflectionMacros.h` provides:

```text
PICO_DECLARE_CLASS
PICO_DEFINE_CLASS
PICO_DEFINE_CLASS_NO_PROPERTIES
PICO_ADD_PROPERTY
```

The macros generate the same `PClass`, construction function, class registration, and member-pointer property expressions that were previously written by hand.

They do not implement:

- source-file scanning
- `PCLASS` or `PPROPERTY` markers
- generated headers or sources
- function reflection
- global static auto-registration

## Example

```cpp
class PExampleObject : public PObject
{
    PICO_DECLARE_CLASS(PExampleObject, PObject)

private:
    int32 Value = 42;
};

PICO_DEFINE_CLASS(PExampleObject)

bool PExampleObject::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Value);
    return Class.AddProperties(std::move(Properties));
}
```

`PICO_DEFINE_CLASS_NO_PROPERTIES` supplies an empty `RegisterProperties` implementation for classes that only need runtime type identity and construction.

## Preserved Runtime Contracts

- Classes still register explicitly through `FClassRegistry`.
- Superclasses must register before derived classes.
- Properties still commit as one transaction.
- Class metadata still becomes immutable after registration.
- Member-pointer and native-owner-token validation remain active.
- `StaticClass` always returns metadata, including invalid metadata that callers may inspect.
- Invalid superclass metadata propagates to derived classes without losing the hierarchy link.
- Object creation and serialization formats do not change.

## Future Header Tool

The macros are intentionally a thin layer over the existing runtime. A future PicoHeaderTool can generate calls to `PClass::Create`, `PProperty::Create`, and `FClassRegistry` without replacing those systems.

Until that tool exists, reflected members remain explicit in `RegisterProperties`. This keeps build order deterministic and avoids pretending that empty annotation macros can discover C++ members by themselves.

## Verification

`PicoObjectTests` verifies:

- macro-derived class names and superclasses
- metadata-driven construction
- private member reflection
- inherited reflected properties
- no-property derived classes
- invalid property diagnostics and superclass failure propagation
- registration after object-system restart

`PDemoCharacter` and the Reflection Demo use the macros, so serialization and Inspector behavior exercise the new authoring path.
