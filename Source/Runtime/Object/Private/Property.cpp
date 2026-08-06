#include "Pico/Object/Property.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"

namespace Pico
{
PProperty::PProperty(
    FName InName,
    EPropertyType InType,
    std::size_t InSize,
    const void* InOwnerTypeToken,
    FMutableAccessor InMutableAccessor,
    FConstAccessor InConstAccessor,
    FPropertyMetadata InMetadata)
    : Name(InName)
    , Type(InType)
    , Metadata(InMetadata)
    , Size(InSize)
    , OwnerTypeToken(InOwnerTypeToken)
    , MutableAccessor(InMutableAccessor)
    , ConstAccessor(InConstAccessor)
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

const FPropertyMetadata& PProperty::GetMetadata() const
{
    return Metadata;
}

EPropertyFlags PProperty::GetFlags() const
{
    return Metadata.Flags;
}

bool PProperty::HasAnyFlags(EPropertyFlags Flags) const
{
    return HasAnyPropertyFlags(Metadata.Flags, Flags);
}

EAssetReferenceType PProperty::GetAssetReferenceType() const
{
    return Metadata.AssetReferenceType;
}

std::size_t PProperty::GetSize() const
{
    return Size;
}

const PClass* PProperty::GetOwnerClass() const
{
    return OwnerClass;
}

bool PProperty::HasValidAccessors() const
{
    return OwnerTypeToken != nullptr && MutableAccessor != nullptr && ConstAccessor != nullptr;
}

const void* PProperty::GetOwnerTypeToken() const
{
    return OwnerTypeToken;
}

void* PProperty::GetValueAddress(PObject* Object, EPropertyType ExpectedType, std::size_t ExpectedSize) const
{
    if (Object == nullptr
        || OwnerClass == nullptr
        || !Object->IsA(OwnerClass)
        || Type != ExpectedType
        || Size != ExpectedSize
        || MutableAccessor == nullptr)
    {
        return nullptr;
    }

    return MutableAccessor(Object);
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
        || Size != ExpectedSize
        || ConstAccessor == nullptr)
    {
        return nullptr;
    }

    return ConstAccessor(Object);
}
}
