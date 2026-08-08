# Month 3.16: Reflected Functions and ProcessEvent

This milestone adds runtime function metadata shared by future RPC, editor commands, and AI tools.
It follows the role of UE's `UFunction` and `UObject::ProcessEvent`, while keeping the first version
small and native-only.

## Runtime Model

`PFunction` stores its declaring `PClass`, name, native owner token, ordered parameter descriptors,
return descriptor, function flags, and a typed thunk generated from a C++ member-function pointer.
`FFunctionValue` is the generic call-frame value and supports scalar values, names, strings, math
types, asset paths, and `PObject*`. Object parameters retain their expected `PClass`.

```text
PClass::FindFunction(Name)
  -> PObject::ProcessEvent(Function, Arguments, Return)
    -> validate target, count, exact types, and object lifetime
      -> typed native thunk
        -> C++ member function
```

Functions use stable `PClass` storage and inherited lookup, like properties. This version rejects
same-name shadowing in derived classes until explicit override semantics are designed.

## Authoring

```cpp
std::vector<PFunction> Functions;
PICO_ADD_FUNCTION(
    Functions,
    SetHealth,
    EFunctionFlags::Callable,
    FName("Health"));
Class.AddFunctions(std::move(Functions));
```

Functions can also use `PFunction::Create<&ThisClass::SetHealth>` directly. Supported parameters
are values or const references. Non-const references, reference returns, and unsupported native
types fail at compile time. Parameter names, duplicate names, owner mismatch, invalid RPC flags,
and mutation after class registration are rejected before invocation.

## Invocation Contract

`ProcessEvent` returns `EFunctionInvokeResult` for invalid function/target, wrong argument count or
type, invalid object arguments, missing return storage, and exceptions caught at the invocation
boundary. CDOs cannot be targets because they are templates outside the live object registry. Null
object arguments are allowed; non-null arguments must be live and compatible with the declared
class.

## RPC Boundary

`Server`, `Client`, `NetMulticast`, and `Reliable` are metadata only in this milestone. They define
the future declaration contract, but `ProcessEvent` currently performs a local native call.
`Reliable` requires a network target, only one target kind may be selected, and RPC return types
must be void. NetDriver ownership, serialization, validation, and remote dispatch remain future
work.

## Verification

Object tests cover mutable and const calls, inherited lookup, value/object returns, macro
registration, diagnostic signatures, all public error results, exception containment, invalid RPC
flags, native owner mismatch, and inherited-name conflicts.

The next object-system milestone is PicoHeaderTool. Generated code should emit this same `PClass`,
`PProperty`, and `PFunction` metadata instead of creating a second reflection implementation.
After HeaderTool and tracing GC establish generated signatures and stable reflected object
references, Pico will add dynamic multicast delegates backed by weak object references, function
names, signature validation, and `ProcessEvent`. Persistent bindings will use repaired scene/object
identities rather than serializing runtime `FObjectHandle` values.
