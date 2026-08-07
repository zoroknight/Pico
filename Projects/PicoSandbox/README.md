# PicoSandbox

PicoSandbox is a project-level example that consumes Pico through public headers and CMake targets. It demonstrates the complete native reflection workflow without a header tool:

```text
declare classes
-> register metadata
-> create an object
-> modify properties through reflection
-> save a .pobj
-> destroy the object
-> load the .pobj
-> verify restored values in PostLoad
```

The project is intentionally outside `Source/Runtime`, `Source/Developer`, and `Source/Editor`. Its game code does not include any Pico `Private` header.

`PicoSandbox.pico` is the project boundary descriptor. Opening it gives Pico two independent roots:

```text
EngineRoot  = <Pico repository>
ProjectRoot = <Pico repository>/Projects/PicoSandbox
```

The sample lives in the engine repository because it is maintained as an engine example. The same
descriptor and directory structure can be copied outside the repository without changing Engine
source code.

Project-authored files live under `Source`, persistent assets under `Content`, and generated/editor
data under `Intermediate` or `Saved`. Editor write-path validation does not allow automatic writes
to either Engine `Source` or project `Source`.

## Project Targets

| Target | Purpose |
| --- | --- |
| `PicoSandboxModule` | Project classes, module entry point, GameInstance, and reusable workflow |
| `PicoSandboxGame` | Project-specific standalone runtime with mapped-input Pawn movement |
| `PicoSandboxDemo` | Readable console walkthrough of the full workflow |
| `PicoSandboxTests` | Automated end-to-end acceptance test |

Build and run from the Pico repository root:

```powershell
cmake --build Build --config Debug --parallel
.\Build\Debug\PicoSandboxGame.exe .\Projects\PicoSandbox\PicoSandbox.pico
.\Build\Projects\PicoSandbox\Debug\PicoSandboxDemo.exe
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
ctest --test-dir Build -C Debug --output-on-failure
```

## Example Types

`PSandboxEntity` declares inherited project state:

```cpp
class PSandboxEntity : public Pico::PObject
{
    PICO_DECLARE_CLASS(PSandboxEntity, Pico::PObject)

private:
    Pico::int32 EntityId = 1001;
    bool bEnabled = true;
};
```

`PSandboxCharacter` derives from it and adds character state:

```cpp
class PSandboxCharacter final : public PSandboxEntity
{
    PICO_DECLARE_CLASS(PSandboxCharacter, PSandboxEntity)

private:
    Pico::int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
    Pico::int32 Mana = 50;
    Pico::FVector3 Velocity;
    Pico::FRotator ViewRotation;
    Pico::FTransform Transform;
};
```

The corresponding `.cpp` defines class boilerplate and explicitly lists reflected members:

```cpp
PICO_DEFINE_CLASS(PSandboxCharacter)

bool PSandboxCharacter::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Health);
    PICO_ADD_PROPERTY(Properties, MoveSpeed);
    PICO_ADD_PROPERTY(Properties, bAlive);
    PICO_ADD_PROPERTY(Properties, Mana);
    PICO_ADD_PROPERTY(Properties, Velocity);
    PICO_ADD_PROPERTY(Properties, ViewRotation);
    PICO_ADD_PROPERTY(Properties, Transform);
    return Class.AddProperties(std::move(Properties));
}
```

Without PicoHeaderTool, these declarations are real C++ code rather than annotations. A future header tool may generate `PICO_DEFINE_CLASS` and `RegisterProperties`, but it will still use the same `PClass`, `PProperty`, and `FClassRegistry` runtime.

## Game Module And Registration

The project owns both its reusable persistence-class registration entry point and its Game Module.
The module registers every project class before the startup map is loaded, then creates the
project-specific GameInstance:

```cpp
bool RegisterSandboxClasses()
{
    return PSandboxEntity::RegisterClass()
        && PSandboxCharacter::RegisterClass();
}

bool FPicoSandboxGameModule::StartupModule()
{
    return RegisterSandboxClasses() && PSandboxPawn::RegisterClass();
}

std::unique_ptr<Pico::FGameInstance> FPicoSandboxGameModule::CreateGameInstance()
{
    return std::make_unique<FSandboxGameInstance>();
}
```

Register base classes before derived classes. `PicoSandboxGame` statically links this module and
passes it to `PicoGameRuntime`; the engine remains unaware of Sandbox types and contains no
project-specific special case.

## Persistence Asset

The normative example asset is:

```text
Content/Objects/SandboxCharacter.pobj
```

`PicoSandboxDemo` and the editor both use this path regardless of the process working directory. The demo writes:

```text
EntityId = 2002
bEnabled = true
Health = 75
MoveSpeed = 720
bAlive = false
Mana = 50
Velocity = (100, 0, 25)
ViewRotation = (5, 90, 0)
Transform.Location = (120, 30, 10)
Transform.Rotation = (10, 45, 0)
Transform.Scale = (1.5, 1, 1)
```

It then destroys the source object, loads the asset, and checks that `PostLoad` observed `Health == 75`.

Automated tests use `Saved/Tests/SandboxWorkflow.pobj` so they do not modify the example asset.

## Editor Workflow

Open the project through the shared Pico editor:

```powershell
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

The editor stores its map at `Content/Maps/EditorWorld.pworld`. Use `Ctrl+S` to save and `Ctrl+O`
to transactionally replace the active World from that file. Project assets remain under
`PicoSandbox/Content`; editor implementation remains under `Source/Editor/PicoEditor`.
Use `Ctrl+C` and `Ctrl+V` to duplicate complete Actors or SceneComponent attachment subtrees;
paste participates in the same Undo/Redo history as other scene edits.

## Add Another Project Class

Use this checklist:

1. Add a public header under `Source/Public/PicoSandbox`.
2. Derive from `PObject` or another registered Pico object class.
3. Add `PICO_DECLARE_CLASS(Type, SuperType)` inside the class.
4. Add `PICO_DEFINE_CLASS(Type)` in one `.cpp`.
5. Add members with `PICO_ADD_PROPERTY` in `RegisterProperties`.
6. Register the superclass before the new class in `SandboxModule.cpp`.
7. Add the `.cpp` to `PicoSandboxModule` in `CMakeLists.txt`.
8. Rebuild and inspect the class through `PicoEditor`.

For a class without local reflected properties, use:

```cpp
PICO_DEFINE_CLASS_NO_PROPERTIES(Type)
```

The class must still be compiled. Runtime creation through `PClass` is dynamic object construction, not runtime creation of a new native C++ type.
