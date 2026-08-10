#include "Pico/Object/Property.h"

#include "Pico/Core/GameThread.h"

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
    FPropertyMetadata InMetadata,
    EObjectReferenceKind InObjectReferenceKind,
    FReferenceAccessor InReferenceAccessor,
    FDynamicMutableAccessor InDynamicMutableAccessor,
    FDynamicConstAccessor InDynamicConstAccessor)
    : Name(InName)
    , Type(InType)
    , Metadata(InMetadata)
    , Size(InSize)
    , OwnerTypeToken(InOwnerTypeToken)
    , MutableAccessor(InMutableAccessor)
    , ConstAccessor(InConstAccessor)
    , ObjectReferenceKind(InObjectReferenceKind)
    , ReferenceAccessor(InReferenceAccessor)
    , DynamicMutableAccessor(InDynamicMutableAccessor)
    , DynamicConstAccessor(InDynamicConstAccessor)
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

EObjectReferenceKind PProperty::GetObjectReferenceKind() const
{
    return ObjectReferenceKind;
}

PObject* PProperty::GetReferencedObject(const PObject* Object) const
{
    return Object != nullptr
        && Type == EPropertyType::Object
        && ReferenceAccessor != nullptr
        ? ResolveObject(ReferenceAccessor(Object))
        : nullptr;
}

FDynamicMulticastDelegate* PProperty::GetDynamicMulticastDelegate(PObject* Object) const
{
    return Object != nullptr
        && OwnerClass != nullptr
        && Object->IsA(OwnerClass)
        && Type == EPropertyType::DynamicMulticastDelegate
        && DynamicMutableAccessor != nullptr
        ? DynamicMutableAccessor(Object)
        : nullptr;
}

const FDynamicMulticastDelegate* PProperty::GetDynamicMulticastDelegate(
    const PObject* Object) const
{
    return Object != nullptr
        && OwnerClass != nullptr
        && Object->IsA(OwnerClass)
        && Type == EPropertyType::DynamicMulticastDelegate
        && DynamicConstAccessor != nullptr
        ? DynamicConstAccessor(Object)
        : nullptr;
}

bool PProperty::NotifyPreChange(
    PObject* Object,
    EPropertyChangeType ChangeType) const
{
    if (Object == nullptr || OwnerClass == nullptr || !Object->IsA(OwnerClass))
    {
        return false;
    }
    Object->NotifyPrePropertyChange({Object, this, ChangeType});
    return true;
}

bool PProperty::NotifyPostChange(
    PObject* Object,
    EPropertyChangeType ChangeType) const
{
    if (Object == nullptr || OwnerClass == nullptr || !Object->IsA(OwnerClass))
    {
        return false;
    }
    Object->NotifyPostPropertyChange({Object, this, ChangeType});
    return true;
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

bool PProperty::SetValueAddress(
    PObject* Object,
    EPropertyType ExpectedType,
    std::size_t ExpectedSize,
    const void* Source,
    FValueCopier Copier,
    EPropertyChangeType ChangeType,
    bool bNotify) const
{
    if (!CheckGameThread("PProperty::SetValue"))
    {
        return false;
    }
    void* Destination = GetValueAddress(Object, ExpectedType, ExpectedSize);
    if (Destination == nullptr || Source == nullptr || Copier == nullptr)
    {
        return false;
    }

    const FPropertyChangedEvent Event {Object, this, ChangeType};
    if (bNotify)
    {
        Object->NotifyPrePropertyChange(Event);
    }
    Copier(Destination, Source);
    if (bNotify)
    {
        Object->NotifyPostPropertyChange(Event);
    }
    return true;
}
}
