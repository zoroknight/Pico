#pragma once

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"

#include <string_view>
#include <type_traits>

namespace Pico
{
PObject* NewObject(const FObjectConstructionParams& Params);
PObject* NewObject(const PClass* Class, PObject* Outer, FName Name, EObjectFlags Flags = EObjectFlags::None);

inline PObject* NewObject(
    const PClass* Class,
    PObject* Outer,
    std::string_view Name,
    EObjectFlags Flags = EObjectFlags::None)
{
    return NewObject(Class, Outer, FName(Name), Flags);
}

template <typename TObject>
TObject* NewObject(PObject* Outer, FName Name, EObjectFlags Flags = EObjectFlags::None)
{
    static_assert(std::is_base_of_v<PObject, TObject>, "NewObject only constructs PObject-derived types");
    return static_cast<TObject*>(NewObject(TObject::StaticClass(), Outer, Name, Flags));
}

template <typename TObject>
TObject* NewObject(PObject* Outer, std::string_view Name, EObjectFlags Flags = EObjectFlags::None)
{
    return NewObject<TObject>(Outer, FName(Name), Flags);
}

template <typename TObject>
const TObject* GetDefault()
{
    static_assert(std::is_base_of_v<PObject, TObject>, "GetDefault only accepts PObject-derived types");
    return static_cast<const TObject*>(TObject::StaticClass()->GetDefaultObject());
}

template <typename TObject>
TObject* GetMutableDefault()
{
    static_assert(std::is_base_of_v<PObject, TObject>, "GetMutableDefault only accepts PObject-derived types");
    return static_cast<TObject*>(TObject::StaticClass()->GetMutableDefaultObject());
}

bool DestroyObject(PObject* Object);
void DestroyObjectTree(PObject* Root);
PObject* ResolveObject(FObjectHandle Handle);
PObject* FindObject(PObject* Outer, FName Name);
bool RenameObject(PObject* Object, FName NewName);
}
