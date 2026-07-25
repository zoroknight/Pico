#include "Pico/Object/Class.h"

#include "Pico/Object/Object.h"

#include <algorithm>
#include <utility>

namespace Pico
{
PClass::PClass(FName InName, const PClass* InSuperClass, std::size_t InSize, FConstructFunction InConstructor)
    : Name(InName)
    , SuperClass(InSuperClass)
    , Size(InSize)
    , Constructor(InConstructor)
{
}

FName PClass::GetName() const
{
    return Name;
}

const PClass* PClass::GetSuperClass() const
{
    return SuperClass;
}

std::size_t PClass::GetSize() const
{
    return Size;
}

bool PClass::IsChildOf(const PClass* Other) const
{
    if (Other == nullptr)
    {
        return false;
    }

    for (const PClass* Current = this; Current != nullptr; Current = Current->GetSuperClass())
    {
        if (Current == Other)
        {
            return true;
        }
    }

    return false;
}

bool PClass::CanConstruct() const
{
    return Constructor != nullptr;
}

FObjectPtr PClass::ConstructObject(const FObjectConstructionParams& Params) const
{
    return Constructor != nullptr ? Constructor(Params) : nullptr;
}

bool PClass::AddProperty(PProperty Property)
{
    const std::size_t TypeSize = GetPropertyTypeSize(Property.GetType());
    if (Property.GetName().IsNone()
        || TypeSize == 0
        || Property.GetSize() != TypeSize
        || Property.GetOffset() > Size
        || Property.GetSize() > Size - Property.GetOffset())
    {
        return false;
    }

    const auto Existing = std::find_if(
        Properties.begin(),
        Properties.end(),
        [&Property](const PProperty& Candidate)
        {
            return Candidate.GetName() == Property.GetName();
        });
    if (Existing != Properties.end())
    {
        return false;
    }

    if (SuperClass != nullptr && SuperClass->FindProperty(Property.GetName()) != nullptr)
    {
        return false;
    }

    Property.OwnerClass = this;
    Properties.push_back(std::move(Property));
    return true;
}

const PProperty* PClass::FindProperty(FName PropertyName) const
{
    for (const PClass* Current = this; Current != nullptr; Current = Current->GetSuperClass())
    {
        const auto Existing = std::find_if(
            Current->Properties.begin(),
            Current->Properties.end(),
            [PropertyName](const PProperty& Property)
            {
                return Property.GetName() == PropertyName;
            });
        if (Existing != Current->Properties.end())
        {
            return &*Existing;
        }
    }

    return nullptr;
}

const std::deque<PProperty>& PClass::GetProperties() const
{
    return Properties;
}
}
