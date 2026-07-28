#include "Pico/Object/ObjectGlobals.h"

#include "Pico/Core/Log.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectSystem.h"

namespace Pico
{
PObject* NewObject(const PClass* Class, PObject* Outer, FName Name, EObjectFlags Flags)
{
    if (!PObjectSystem::IsInitialized())
    {
        PICO_LOG(LogObject, Error, "NewObject called before the object system was initialized");
        return nullptr;
    }

    if (Class == nullptr || FClassRegistry::FindClass(Class->GetName()) != Class)
    {
        PICO_LOG(LogObject, Error, "NewObject requires a registered class");
        return nullptr;
    }

    if (!Class->CanConstruct())
    {
        PICO_LOG(LogObject, Error, "Class '{}' cannot construct objects", Class->GetName().ToString());
        return nullptr;
    }

    if (Name.IsNone())
    {
        PICO_LOG(LogObject, Error, "NewObject requires a non-None object name");
        return nullptr;
    }

    if (Outer != nullptr
        && (ResolveObject(Outer->GetHandle()) != Outer || Outer->IsBeginningDestroy()))
    {
        PICO_LOG(LogObject, Error, "NewObject requires a live outer that is not being destroyed");
        return nullptr;
    }

    if (FindObject(Outer, Name) != nullptr)
    {
        PICO_LOG(LogObject, Error, "Object '{}' already exists in the requested outer", Name.ToString());
        return nullptr;
    }

    const FObjectConstructionParams Params { Class, Outer, Name, Flags };
    FObjectPtr Object = Class->ConstructObject(Params);
    if (Object == nullptr || Object->GetClass() != Class)
    {
        PICO_LOG(LogObject, Error, "Class '{}' returned an invalid object instance", Class->GetName().ToString());
        return nullptr;
    }

    return FObjectRegistry::AddObject(std::move(Object));
}

bool DestroyObject(PObject* Object)
{
    return FObjectRegistry::DestroyObject(Object);
}

void DestroyObjectTree(PObject* Root)
{
    FObjectRegistry::DestroyObjectTree(Root);
}

PObject* ResolveObject(FObjectHandle Handle)
{
    return FObjectRegistry::ResolveObject(Handle);
}

PObject* FindObject(PObject* Outer, FName Name)
{
    return FObjectRegistry::FindObject(Outer, Name);
}
}
