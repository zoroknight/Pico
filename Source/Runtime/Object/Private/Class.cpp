#include "Pico/Object/Class.h"

#include "Pico/Object/Object.h"

#include <algorithm>
#include <utility>

namespace Pico
{
PClass::PClass(FName InName, const PClass* InSuperClass, std::size_t InSize, FConstructFunction InConstructor)
    : PClass(InName, InSuperClass, InSize, InConstructor, nullptr)
{
}

PClass::PClass(
    FName InName,
    const PClass* InSuperClass,
    std::size_t InSize,
    FConstructFunction InConstructor,
    const void* InNativeTypeToken)
    : Name(InName)
    , SuperClass(InSuperClass)
    , Size(InSize)
    , Constructor(InConstructor)
    , NativeTypeToken(InNativeTypeToken)
{
    if (SuperClass != nullptr && !SuperClass->IsMetadataValid())
    {
        bMetadataValid = false;
        MetadataError = EClassMetadataError::InvalidSuperClass;
    }
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
    std::vector<PProperty> NewProperties;
    NewProperties.push_back(std::move(Property));
    return AddProperties(std::move(NewProperties));
}

bool PClass::AddProperties(std::vector<PProperty> InProperties)
{
    if (bMetadataFinalized || !bMetadataValid || InProperties.empty())
    {
        return false;
    }

    for (std::size_t Index = 0; Index < InProperties.size(); ++Index)
    {
        if (!ValidateProperty(
                InProperties[Index],
                std::span<const PProperty>(InProperties.data(), Index)))
        {
            bMetadataValid = false;
            MetadataError = EClassMetadataError::InvalidProperty;
            return false;
        }
    }

    for (PProperty& Property : InProperties)
    {
        Property.OwnerClass = this;
        Properties.push_back(std::move(Property));
    }
    return true;
}

bool PClass::IsMetadataValid() const
{
    return bMetadataValid;
}

bool PClass::IsMetadataFinalized() const
{
    return bMetadataFinalized;
}

EClassMetadataError PClass::GetMetadataError() const
{
    return MetadataError;
}

bool PClass::ValidateProperty(
    const PProperty& Property,
    std::span<const PProperty> PendingProperties) const
{
    const std::size_t TypeSize = GetPropertyTypeSize(Property.GetType());
    const bool bSerializable =
        Property.HasAnyFlags(EPropertyFlags::Serializable);
    const bool bTransient =
        Property.HasAnyFlags(EPropertyFlags::Transient);
    const bool bValidAssetMetadata =
        Property.GetAssetReferenceType() == EAssetReferenceType::None
        || Property.GetType() == EPropertyType::AssetPath;
    if (Property.GetName().IsNone()
        || TypeSize == 0
        || Property.GetSize() != TypeSize
        || Property.GetOwnerTypeToken() != NativeTypeToken
        || !Property.HasValidAccessors()
        || (bSerializable && bTransient)
        || !bValidAssetMetadata)
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

    const auto PendingDuplicate = std::find_if(
        PendingProperties.begin(),
        PendingProperties.end(),
        [&Property](const PProperty& Candidate)
        {
            return Candidate.GetName() == Property.GetName();
        });
    if (PendingDuplicate != PendingProperties.end())
    {
        return false;
    }

    if (SuperClass != nullptr && SuperClass->FindProperty(Property.GetName()) != nullptr)
    {
        return false;
    }

    return true;
}

bool PClass::FinalizeMetadata() const
{
    if (!bMetadataValid)
    {
        return false;
    }

    bMetadataFinalized = true;
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
