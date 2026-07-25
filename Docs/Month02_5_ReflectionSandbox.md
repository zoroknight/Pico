# Pico Month 02.5: Reflection Sandbox And Inspector

Month 02.5 adds learning and observation tools on top of the Month 02 object, reflection, and serialization runtime.

## Dependency Direction

```text
PicoInspector
  -> PicoReflectionSample
  -> PicoReflectionTools
  -> PicoObject
  -> PicoCore
```

`PicoObject` does not depend on the sample, inspector, GLFW, OpenGL, or Dear ImGui.

## Registry Observation

`FClassRegistry::GetClasses` returns a name-sorted snapshot of registered `PClass` pointers.

`FObjectRegistry::GetObjects` returns a slot-ordered snapshot of live `PObject` pointers.

The returned vectors do not expose registry storage and do not transfer ownership. The existing main-thread-only object-system rule still applies.

## Reflection Tools

`PicoReflectionTools` provides:

```text
GetAllProperties
DumpClass
DumpObject
GetPropertyTypeName
```

Inherited properties are gathered from base class to derived class. Object dumps read values through `PProperty`, not through concrete C++ members.

## Reflection Sample

`PicoReflectionSample` contains `PDemoCharacter`, the first non-test reflected type. It demonstrates explicit class metadata, constructor linkage, property registration, and `PostLoad`.

`PicoReflectionDemo` runs the complete chain:

```text
register
-> create
-> inspect
-> edit through PProperty
-> save
-> destroy
-> load
-> PostLoad
-> inspect restored values
```

## Pico Inspector

`PicoInspector` uses Dear ImGui with GLFW and OpenGL 3. These dependencies belong only to the tool executable.

The first interface contains:

- a class list backed by Class Registry
- an object list backed by Object Registry
- a details panel backed by `PProperty`
- Create, Destroy, Save, and Load commands
- serialization error status

The inspector is not yet a world editor. It has no viewport, scene hierarchy, asset browser, undo system, CDO editing, or object-reference editing.

## Third-Party Code

Dear ImGui and GLFW are stored under `ThirdParty`. Their original license files are preserved. Third-party implementation files are not part of Pico's runtime architecture.
