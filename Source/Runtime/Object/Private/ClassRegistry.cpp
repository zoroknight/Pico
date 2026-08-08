#include "Pico/Object/ClassRegistry.h"

#include "Pico/Core/Log.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Pico
{
namespace
{
using FClassMap = std::unordered_map<FName, const PClass*, FNameHash>;

FClassMap& GetClassMap()
{
    static FClassMap Classes;
    return Classes;
}
}

bool FClassRegistry::RegisterClass(const PClass* Class)
{
    if (Class == nullptr || Class->GetName().IsNone())
    {
        PICO_LOG(LogObject, Error, "Cannot register a null or unnamed class");
        return false;
    }

    FClassMap& Classes = GetClassMap();
    const PClass* SuperClass = Class->GetSuperClass();
    if (!Class->IsMetadataValid())
    {
        if (Class->GetMetadataError() == EClassMetadataError::InvalidSuperClass && SuperClass != nullptr)
        {
            PICO_LOG(
                LogObject,
                Error,
                "Class '{}' inherits invalid metadata from superclass '{}'",
                Class->GetName().ToString(),
                SuperClass->GetName().ToString());
        }
        else
        {
            PICO_LOG(LogObject, Error, "Class '{}' has invalid property metadata", Class->GetName().ToString());
        }
        return false;
    }

    if (SuperClass != nullptr && FindClass(SuperClass->GetName()) != SuperClass)
    {
        PICO_LOG(
            LogObject,
            Error,
            "Class '{}' requires its superclass '{}' to be registered first",
            Class->GetName().ToString(),
            SuperClass->GetName().ToString());
        return false;
    }

    const auto Existing = Classes.find(Class->GetName());
    if (Existing != Classes.end())
    {
        if (Existing->second == Class)
        {
            return true;
        }

        PICO_LOG(LogObject, Error, "Class name '{}' is already registered", Class->GetName().ToString());
        return false;
    }

    if (!Class->FinalizeMetadata())
    {
        PICO_LOG(LogObject, Error, "Class '{}' metadata could not be finalized", Class->GetName().ToString());
        return false;
    }

    Classes.emplace(Class->GetName(), Class);
    bool bDefaultObjectCreated = false;
    try
    {
        bDefaultObjectCreated = Class->CreateDefaultObject();
    }
    catch (...)
    {
        bDefaultObjectCreated = false;
    }
    if (!bDefaultObjectCreated)
    {
        Classes.erase(Class->GetName());
        PICO_LOG(LogObject, Error, "Class '{}' could not create its default object", Class->GetName().ToString());
        return false;
    }
    return true;
}

const PClass* FClassRegistry::FindClass(FName Name)
{
    const FClassMap& Classes = GetClassMap();
    const auto Existing = Classes.find(Name);
    return Existing != Classes.end() ? Existing->second : nullptr;
}

std::vector<const PClass*> FClassRegistry::GetClasses()
{
    std::vector<const PClass*> Result;
    Result.reserve(GetClassMap().size());
    for (const auto& [Name, Class] : GetClassMap())
    {
        (void)Name;
        Result.push_back(Class);
    }

    std::sort(
        Result.begin(),
        Result.end(),
        [](const PClass* Left, const PClass* Right)
        {
            return Left->GetName().ToString() < Right->GetName().ToString();
        });
    return Result;
}

std::size_t FClassRegistry::GetClassCount()
{
    return GetClassMap().size();
}

void FClassRegistry::Clear()
{
    for (const auto& [Name, Class] : GetClassMap())
    {
        (void)Name;
        if (Class != nullptr)
        {
            Class->ResetDefaultObject();
        }
    }
    GetClassMap().clear();
}
}
