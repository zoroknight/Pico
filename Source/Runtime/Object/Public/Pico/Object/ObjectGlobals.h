#pragma once

#include "Pico/Object/Object.h"

#include <string_view>
#include <type_traits>

namespace Pico
{
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

bool DestroyObject(PObject* Object);
void DestroyObjectTree(PObject* Root);
PObject* ResolveObject(FObjectHandle Handle);
PObject* FindObject(PObject* Outer, FName Name);
}
