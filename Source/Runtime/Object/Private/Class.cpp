#include "Pico/Object/Class.h"

#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectInitializer.h"

#include <algorithm>
#include <utility>

namespace Pico
{
std::unique_ptr<PClass> PClass::CreateDynamicDerived(
    FName InName,
    const PClass* InSuperClass)
{
    if (InName.IsNone() || InSuperClass == nullptr
        || !InSuperClass->CanConstruct())
    {
        return {};
    }
    return std::unique_ptr<PClass>(new PClass(
        InName,
        InSuperClass,
        InSuperClass->Size,
        InSuperClass->Constructor,
        InSuperClass->NativeTypeToken));
}

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

const PObject* PClass::GetDefaultObject() const
{
    return ClassDefaultObject.get();
}

PObject* PClass::GetMutableDefaultObject() const
{
    return ClassDefaultObject.get();
}

const std::vector<FDefaultSubobjectRecord>& PClass::GetDefaultSubobjects() const
{
    return DefaultSubobjects;
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

bool PClass::AddFunction(PFunction Function)
{
    std::vector<PFunction> NewFunctions;
    NewFunctions.push_back(std::move(Function));
    return AddFunctions(std::move(NewFunctions));
}

bool PClass::AddFunctions(std::vector<PFunction> InFunctions)
{
    if (bMetadataFinalized || !bMetadataValid || InFunctions.empty())
    {
        return false;
    }

    for (std::size_t Index = 0; Index < InFunctions.size(); ++Index)
    {
        if (!ValidateFunction(
                InFunctions[Index],
                std::span<const PFunction>(InFunctions.data(), Index)))
        {
            bMetadataValid = false;
            MetadataError = EClassMetadataError::InvalidFunction;
            return false;
        }
    }

    for (PFunction& Function : InFunctions)
    {
        Function.OwnerClass = this;
        Functions.push_back(std::move(Function));
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
    const bool bDynamicDelegate =
        Property.GetType() == EPropertyType::DynamicMulticastDelegate;
    const bool bSerializable =
        Property.HasAnyFlags(EPropertyFlags::Serializable);
    const bool bTransient =
        Property.HasAnyFlags(EPropertyFlags::Transient);
    const bool bValidAssetMetadata =
        Property.GetAssetReferenceType() == EAssetReferenceType::None
        || Property.GetType() == EPropertyType::AssetPath;
    const bool bIsObjectReference = Property.GetType() == EPropertyType::Object;
    const bool bValidObjectReference = bIsObjectReference
        ? Property.GetObjectReferenceKind() != EObjectReferenceKind::None
            && Property.HasReferencedObjectClassResolver()
            && bTransient
            && !bSerializable
        : Property.GetObjectReferenceKind() == EObjectReferenceKind::None;
    if (Property.GetName().IsNone()
        || (!bDynamicDelegate && TypeSize == 0)
        || (bDynamicDelegate ? Property.GetSize() == 0 : Property.GetSize() != TypeSize)
        || Property.GetOwnerTypeToken() != NativeTypeToken
        || !Property.HasValidAccessors()
        || (bSerializable && bTransient)
        || !bValidAssetMetadata
        || !bValidObjectReference)
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

bool PClass::ValidateFunction(
    const PFunction& Function,
    std::span<const PFunction> PendingFunctions) const
{
    if (!Function.IsMetadataValid()
        || Function.OwnerTypeToken != NativeTypeToken)
    {
        return false;
    }

    const auto HasName = [&Function](const PFunction& Candidate)
    {
        return Candidate.GetName() == Function.GetName();
    };
    if (std::find_if(Functions.begin(), Functions.end(), HasName) != Functions.end()
        || std::find_if(PendingFunctions.begin(), PendingFunctions.end(), HasName) != PendingFunctions.end()
        || (SuperClass != nullptr && SuperClass->FindFunction(Function.GetName()) != nullptr))
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

    StrongReferenceProperties.clear();
    std::vector<const PClass*> Hierarchy;
    for (const PClass* Current = this; Current != nullptr;
        Current = Current->GetSuperClass())
    {
        Hierarchy.push_back(Current);
    }
    for (auto It = Hierarchy.rbegin(); It != Hierarchy.rend(); ++It)
    {
        for (const PProperty& Property : (*It)->Properties)
        {
            if (Property.GetObjectReferenceKind()
                == EObjectReferenceKind::Strong)
            {
                StrongReferenceProperties.push_back(&Property);
            }
        }
    }
    bMetadataFinalized = true;
    return true;
}

bool PClass::CreateDefaultObject() const
{
    if (ClassDefaultObject != nullptr)
    {
        return true;
    }
    if (!bMetadataFinalized || !bMetadataValid || !CanConstruct())
    {
        return false;
    }
    if (SuperClass != nullptr && SuperClass->GetDefaultObject() == nullptr)
    {
        return false;
    }

    const FName DefaultObjectName("Default__" + Name.ToString());
    const FObjectConstructionParams Params {
        this,
        nullptr,
        DefaultObjectName,
        EObjectFlags::Transient | EObjectFlags::ClassDefaultObject | EObjectFlags::RootSet,
        nullptr
    };
    FObjectPtr Object = ConstructObject(Params);
    if (Object == nullptr
        || Object->GetClass() != this
        || !HasAnyFlags(Object->GetFlags(), EObjectFlags::ClassDefaultObject))
    {
        return false;
    }

    ClassDefaultObject = std::move(Object);
    FObjectInitializer Initializer(
        ClassDefaultObject.get(),
        SuperClass != nullptr ? SuperClass->GetDefaultObject() : nullptr);
    if (!Initializer.InitializeClassDefaultObject(*const_cast<PClass*>(this)))
    {
        DefaultSubobjects.clear();
        ClassDefaultObject.reset();
        return false;
    }
    return true;
}

void PClass::ResetDefaultObject() const
{
    DefaultSubobjects.clear();
    ClassDefaultObject.reset();
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

std::span<const PProperty* const> PClass::GetStrongReferenceProperties() const
{
    return StrongReferenceProperties;
}

const PFunction* PClass::FindFunction(FName FunctionName) const
{
    for (const PClass* Current = this; Current != nullptr; Current = Current->GetSuperClass())
    {
        const auto Existing = std::find_if(
            Current->Functions.begin(),
            Current->Functions.end(),
            [FunctionName](const PFunction& Function)
            {
                return Function.GetName() == FunctionName;
            });
        if (Existing != Current->Functions.end())
        {
            return &*Existing;
        }
    }
    return nullptr;
}

const std::deque<PFunction>& PClass::GetFunctions() const
{
    return Functions;
}
}
