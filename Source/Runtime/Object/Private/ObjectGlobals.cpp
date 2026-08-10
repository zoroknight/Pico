#include "Pico/Object/ObjectGlobals.h"

#include "Pico/Core/Log.h"
#include "Pico/Core/GameThread.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/ObjectInitializer.h"
#include "Pico/Object/ObjectSystem.h"

namespace Pico
{
PObject* NewObject(const FObjectConstructionParams& Params)
{
    if (!CheckGameThread("NewObject"))
    {
        return nullptr;
    }
    if (!PObjectSystem::IsInitialized())
    {
        PICO_LOG(LogObject, Error, "NewObject called before the object system was initialized");
        return nullptr;
    }

    const PClass* Class = Params.Class;
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

    if (Params.Name.IsNone())
    {
        PICO_LOG(LogObject, Error, "NewObject requires a non-None object name");
        return nullptr;
    }

    if (HasAnyFlags(Params.Flags, EObjectFlags::ClassDefaultObject))
    {
        PICO_LOG(LogObject, Error, "NewObject cannot create a class default object");
        return nullptr;
    }

    if (Params.Outer != nullptr
        && (ResolveObject(Params.Outer->GetHandle()) != Params.Outer || Params.Outer->IsBeginningDestroy()))
    {
        PICO_LOG(LogObject, Error, "NewObject requires a live outer that is not being destroyed");
        return nullptr;
    }

    if (FindObject(Params.Outer, Params.Name) != nullptr)
    {
        PICO_LOG(LogObject, Error, "Object '{}' already exists in the requested outer", Params.Name.ToString());
        return nullptr;
    }

    FObjectPtr Object = Class->ConstructObject(Params);
    if (Object == nullptr || Object->GetClass() != Class)
    {
        PICO_LOG(LogObject, Error, "Class '{}' returned an invalid object instance", Class->GetName().ToString());
        return nullptr;
    }

    const PObject* Template = Params.Template != nullptr
        ? Params.Template
        : Class->GetDefaultObject();
    const bool bUsesClassDefault = Template == Class->GetDefaultObject();
    const bool bUsesLiveObject = Template != nullptr
        && ResolveObject(Template->GetHandle()) == Template
        && !Template->IsBeginningDestroy();
    const bool bUsesDefaultSubobjectTemplate = Template != nullptr
        && HasAnyFlags(Template->GetFlags(), EObjectFlags::DefaultSubobject)
        && !Template->GetHandle().IsValid()
        && Template->GetOuter() != nullptr
        && HasAnyFlags(Template->GetOuter()->GetFlags(), EObjectFlags::ClassDefaultObject);
    if (Template == nullptr
        || Template->GetClass() != Class
        || (!bUsesClassDefault && !bUsesLiveObject && !bUsesDefaultSubobjectTemplate))
    {
        PICO_LOG(LogObject, Error, "Class '{}' has no compatible default template", Class->GetName().ToString());
        return nullptr;
    }
    if (!FObjectInitializer(Object.get(), Template).InitializeProperties())
    {
        PICO_LOG(LogObject, Error, "Class '{}' failed to initialize reflected defaults", Class->GetName().ToString());
        return nullptr;
    }

    PObject* RawObject = FObjectRegistry::AddObject(std::move(Object), true);
    if (RawObject == nullptr)
    {
        return nullptr;
    }

    if (!FObjectInitializer(RawObject, Template).InitializeDefaultSubobjectInstances())
    {
        FObjectRegistry::DestroyObjectTree(RawObject);
        PICO_LOG(LogObject, Error, "Class '{}' failed to initialize default subobjects", Class->GetName().ToString());
        return nullptr;
    }

    FObjectRegistry::PostInitObject(RawObject);
    return RawObject;
}

PObject* NewObject(const PClass* Class, PObject* Outer, FName Name, EObjectFlags Flags)
{
    return NewObject(FObjectConstructionParams { Class, Outer, Name, Flags, nullptr });
}

bool DestroyObject(PObject* Object)
{
    if (!CheckGameThread("DestroyObject"))
    {
        return false;
    }
    return FObjectRegistry::DestroyObject(Object);
}

void DestroyObjectTree(PObject* Root)
{
    if (!CheckGameThread("DestroyObjectTree"))
    {
        return;
    }
    FObjectRegistry::DestroyObjectTree(Root);
}

PObject* ResolveObject(FObjectHandle Handle)
{
    if (!CheckGameThread("ResolveObject"))
    {
        return nullptr;
    }
    return FObjectRegistry::ResolveObject(Handle);
}

PObject* FindObject(PObject* Outer, FName Name)
{
    if (!CheckGameThread("FindObject"))
    {
        return nullptr;
    }
    return FObjectRegistry::FindObject(Outer, Name);
}

bool RenameObject(PObject* Object, FName NewName)
{
    if (!CheckGameThread("RenameObject"))
    {
        return false;
    }
    return FObjectRegistry::RenameObject(Object, NewName);
}
}
