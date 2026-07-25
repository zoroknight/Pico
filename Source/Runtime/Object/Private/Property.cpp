#include "Pico/Object/Property.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"

#include <cstddef>

namespace Pico
{
PProperty::PProperty(FName InName, EPropertyType InType, std::size_t InOffset, std::size_t InSize)
    : Name(InName)
    , Type(InType)
    , Offset(InOffset)
    , Size(InSize)
{
}

FName PProperty::GetName() const
{
    return Name;
}

EPropertyType PProperty::GetType() const
{
    return Type;
}

std::size_t PProperty::GetOffset() const
{
    return Offset;
}

std::size_t PProperty::GetSize() const
{
    return Size;
}

const PClass* PProperty::GetOwnerClass() const
{
    return OwnerClass;
}

void* PProperty::GetValueAddress(PObject* Object, EPropertyType ExpectedType, std::size_t ExpectedSize) const
{
    return const_cast<void*>(GetValueAddress(
        static_cast<const PObject*>(Object),
        ExpectedType,
        ExpectedSize));
}

const void* PProperty::GetValueAddress(
    const PObject* Object,
    EPropertyType ExpectedType,
    std::size_t ExpectedSize) const
{
    if (Object == nullptr
        || OwnerClass == nullptr
        || !Object->IsA(OwnerClass)
        || Type != ExpectedType
        || Size != ExpectedSize)
    {
        return nullptr;
    }

    const PClass* ObjectClass = Object->GetClass();
    if (ObjectClass == nullptr || Offset > ObjectClass->GetSize() || Size > ObjectClass->GetSize() - Offset)
    {
        return nullptr;
    }

    const auto* ObjectBytes = reinterpret_cast<const std::byte*>(Object);
    return ObjectBytes + Offset;
}
}
