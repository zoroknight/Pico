#include "Pico/Object/ObjectInitializer.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectName.h"
#include "Pico/Object/Property.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Pico
{
namespace
{
void GatherProperties(const PClass* Class, std::vector<const PProperty*>& OutProperties)
{
    if (Class == nullptr)
    {
        return;
    }

    GatherProperties(Class->GetSuperClass(), OutProperties);
    for (const PProperty& Property : Class->GetProperties())
    {
        OutProperties.push_back(&Property);
    }
}

bool CopyProperty(const PProperty& Property, const PObject* Source, PObject* Destination)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        return Property.GetValue(Source, Value) && Property.SetValue(Destination, Value);
    }
    }

    return false;
}
}

FObjectInitializer::FObjectInitializer(PObject* InObject, const PObject* InTemplate)
    : Object(InObject)
    , Template(InTemplate)
{
}

PObject* FObjectInitializer::GetObject() const
{
    return Object;
}

const PObject* FObjectInitializer::GetTemplate() const
{
    return Template;
}

bool FObjectInitializer::InitializeProperties() const
{
    if (Object == nullptr || Template == nullptr
        || Object->GetClass() == nullptr
        || Object->GetClass() != Template->GetClass())
    {
        return false;
    }

    std::vector<const PProperty*> Properties;
    GatherProperties(Object->GetClass(), Properties);
    for (const PProperty* Property : Properties)
    {
        if (Property == nullptr || Property->HasAnyFlags(EPropertyFlags::Transient))
        {
            continue;
        }
        if (!CopyProperty(*Property, Template, Object))
        {
            return false;
        }
    }
    return true;
}

PObject* FObjectInitializer::CreateDefaultSubobject(const PClass* Class, FName Name)
{
    if (Object == nullptr
        || Class == nullptr
        || !IsValidObjectName(Name)
        || !HasAnyFlags(Object->GetFlags(), EObjectFlags::ClassDefaultObject)
        || Object->GetClass() == nullptr
        || Object->GetClass()->GetDefaultObject() != Object
        || FClassRegistry::FindClass(Class->GetName()) != Class)
    {
        return nullptr;
    }

    PClass* OwnerClass = const_cast<PClass*>(Object->GetClass());
    const auto Existing = std::find_if(
        OwnerClass->DefaultSubobjects.begin(),
        OwnerClass->DefaultSubobjects.end(),
        [Name](const FDefaultSubobjectRecord& Record)
        {
            return Record.Name == Name;
        });
    if (Existing != OwnerClass->DefaultSubobjects.end())
    {
        return Existing->Class == Class ? Existing->Template.get() : nullptr;
    }

    const PObject* ClassDefault = Class->GetDefaultObject();
    if (ClassDefault == nullptr)
    {
        return nullptr;
    }

    const FObjectConstructionParams Params {
        Class,
        Object,
        Name,
        EObjectFlags::Transient | EObjectFlags::DefaultSubobject | EObjectFlags::RootSet,
        ClassDefault
    };
    FObjectPtr Subobject = Class->ConstructObject(Params);
    if (Subobject == nullptr
        || Subobject->GetClass() != Class
        || !FObjectInitializer(Subobject.get(), ClassDefault).InitializeProperties())
    {
        return nullptr;
    }

    PObject* Result = Subobject.get();
    FDefaultSubobjectRecord Record;
    Record.Name = Name;
    Record.Class = Class;
    Record.Template = std::move(Subobject);
    OwnerClass->DefaultSubobjects.push_back(std::move(Record));
    return Result;
}

bool FObjectInitializer::SetRootSubobject(PObject* Subobject)
{
    if (Object == nullptr || Object->GetClass() == nullptr || Subobject == nullptr)
    {
        return false;
    }

    PClass* OwnerClass = const_cast<PClass*>(Object->GetClass());
    auto Existing = std::find_if(
        OwnerClass->DefaultSubobjects.begin(),
        OwnerClass->DefaultSubobjects.end(),
        [Subobject](const FDefaultSubobjectRecord& Record)
        {
            return Record.Template.get() == Subobject;
        });
    if (Existing == OwnerClass->DefaultSubobjects.end())
    {
        return false;
    }

    for (FDefaultSubobjectRecord& Record : OwnerClass->DefaultSubobjects)
    {
        Record.bIsRoot = false;
    }
    Existing->bIsRoot = true;
    Existing->AttachParentName = {};
    Existing->AttachSocketName = {};
    return true;
}

bool FObjectInitializer::AttachSubobject(
    PObject* Subobject,
    PObject* AttachParent,
    FName SocketName)
{
    if (Object == nullptr
        || Object->GetClass() == nullptr
        || Subobject == nullptr
        || AttachParent == nullptr
        || Subobject == AttachParent)
    {
        return false;
    }

    PClass* OwnerClass = const_cast<PClass*>(Object->GetClass());
    auto FindByTemplate =
        [OwnerClass](PObject* Candidate)
        {
            return std::find_if(
                OwnerClass->DefaultSubobjects.begin(),
                OwnerClass->DefaultSubobjects.end(),
                [Candidate](const FDefaultSubobjectRecord& Record)
                {
                    return Record.Template.get() == Candidate;
                });
        };
    auto ChildRecord = FindByTemplate(Subobject);
    const auto ParentRecord = FindByTemplate(AttachParent);
    if (ChildRecord == OwnerClass->DefaultSubobjects.end()
        || ParentRecord == OwnerClass->DefaultSubobjects.end())
    {
        return false;
    }

    FName AncestorName = ParentRecord->Name;
    while (!AncestorName.IsNone())
    {
        if (AncestorName == ChildRecord->Name)
        {
            return false;
        }
        const auto Ancestor = std::find_if(
            OwnerClass->DefaultSubobjects.begin(),
            OwnerClass->DefaultSubobjects.end(),
            [AncestorName](const FDefaultSubobjectRecord& Record)
            {
                return Record.Name == AncestorName;
            });
        AncestorName = Ancestor != OwnerClass->DefaultSubobjects.end()
            ? Ancestor->AttachParentName
            : FName {};
    }

    ChildRecord->bIsRoot = false;
    ChildRecord->AttachParentName = ParentRecord->Name;
    ChildRecord->AttachSocketName = SocketName;
    return true;
}

bool FObjectInitializer::InitializeClassDefaultObject(PClass& Class) const
{
    if (Object == nullptr
        || Object != Class.GetDefaultObject()
        || !HasAnyFlags(Object->GetFlags(), EObjectFlags::ClassDefaultObject))
    {
        return false;
    }

    if (const PClass* SuperClass = Class.GetSuperClass())
    {
        for (const FDefaultSubobjectRecord& SuperRecord : SuperClass->GetDefaultSubobjects())
        {
            const FObjectConstructionParams Params {
                SuperRecord.Class,
                Object,
                SuperRecord.Name,
                EObjectFlags::Transient | EObjectFlags::DefaultSubobject | EObjectFlags::RootSet,
                SuperRecord.Template.get()
            };
            FObjectPtr Subobject = SuperRecord.Class->ConstructObject(Params);
            if (Subobject == nullptr
                || !FObjectInitializer(Subobject.get(), SuperRecord.Template.get()).InitializeProperties())
            {
                return false;
            }

            FDefaultSubobjectRecord Record;
            Record.Name = SuperRecord.Name;
            Record.Class = SuperRecord.Class;
            Record.AttachParentName = SuperRecord.AttachParentName;
            Record.AttachSocketName = SuperRecord.AttachSocketName;
            Record.bIsRoot = SuperRecord.bIsRoot;
            Record.Template = std::move(Subobject);
            Class.DefaultSubobjects.push_back(std::move(Record));
        }
    }

    return Object->DefineDefaultSubobjects(*const_cast<FObjectInitializer*>(this));
}

bool FObjectInitializer::InitializeDefaultSubobjectInstances() const
{
    if (Object == nullptr || Object->GetClass() == nullptr)
    {
        return false;
    }

    const std::vector<FDefaultSubobjectRecord>& Records =
        Object->GetClass()->GetDefaultSubobjects();
    std::unordered_map<FName, PObject*, FNameHash> Instances;
    Instances.reserve(Records.size());
    for (const FDefaultSubobjectRecord& Record : Records)
    {
        const FObjectConstructionParams Params {
            Record.Class,
            Object,
            Record.Name,
            EObjectFlags::DefaultSubobject,
            Record.Template.get()
        };
        PObject* Subobject = NewObject(Params);
        if (Subobject == nullptr
            || !Instances.emplace(Record.Name, Subobject).second
            || !Object->OnDefaultSubobjectCreated(Subobject))
        {
            return false;
        }
    }

    for (const FDefaultSubobjectRecord& Record : Records)
    {
        PObject* Subobject = Instances.at(Record.Name);
        PObject* AttachParent = nullptr;
        if (!Record.AttachParentName.IsNone())
        {
            const auto Parent = Instances.find(Record.AttachParentName);
            if (Parent == Instances.end())
            {
                return false;
            }
            AttachParent = Parent->second;
        }
        if (!Object->OnDefaultSubobjectRelation(
                Subobject,
                AttachParent,
                Record.AttachSocketName,
                Record.bIsRoot))
        {
            return false;
        }
    }
    return true;
}
}
