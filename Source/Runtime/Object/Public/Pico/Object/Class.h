#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/Property.h"

#include <cstddef>
#include <deque>
#include <span>
#include <vector>

namespace Pico
{
class PObject;
class FObjectInitializer;
class PClass;
struct FObjectConstructionParams;

struct FDefaultSubobjectRecord
{
    FName Name;
    const PClass* Class = nullptr;
    FName AttachParentName;
    FName AttachSocketName;
    bool bIsRoot = false;
    FObjectPtr Template;
};

enum class EClassMetadataError
{
    None,
    InvalidProperty,
    InvalidFunction,
    InvalidSuperClass
};

class PClass
{
public:
    using FConstructFunction = FObjectPtr (*)(const FObjectConstructionParams&);

    PClass(FName InName, const PClass* InSuperClass, std::size_t InSize, FConstructFunction InConstructor);

    template <typename TObject>
    static PClass Create(
        FName InName,
        const PClass* InSuperClass,
        std::size_t InSize,
        FConstructFunction InConstructor)
    {
        static_assert(std::is_base_of_v<PObject, TObject>, "PClass native types must derive from PObject");
        return PClass(
            InName,
            InSuperClass,
            InSize,
            InConstructor,
            Detail::GetNativeTypeToken<TObject>());
    }

    FName GetName() const;
    const PClass* GetSuperClass() const;
    std::size_t GetSize() const;
    bool IsChildOf(const PClass* Other) const;
    bool CanConstruct() const;
    FObjectPtr ConstructObject(const FObjectConstructionParams& Params) const;
    const PObject* GetDefaultObject() const;
    PObject* GetMutableDefaultObject() const;
    const std::vector<FDefaultSubobjectRecord>& GetDefaultSubobjects() const;
    bool AddProperty(PProperty Property);
    bool AddProperties(std::vector<PProperty> InProperties);
    bool AddFunction(PFunction Function);
    bool AddFunctions(std::vector<PFunction> InFunctions);
    bool IsMetadataValid() const;
    bool IsMetadataFinalized() const;
    EClassMetadataError GetMetadataError() const;
    const PProperty* FindProperty(FName PropertyName) const;
    const std::deque<PProperty>& GetProperties() const;
    const PFunction* FindFunction(FName FunctionName) const;
    const std::deque<PFunction>& GetFunctions() const;

private:
    PClass(
        FName InName,
        const PClass* InSuperClass,
        std::size_t InSize,
        FConstructFunction InConstructor,
        const void* InNativeTypeToken);

    bool ValidateProperty(const PProperty& Property, std::span<const PProperty> PendingProperties) const;
    bool ValidateFunction(const PFunction& Function, std::span<const PFunction> PendingFunctions) const;
    bool FinalizeMetadata() const;
    bool CreateDefaultObject() const;
    void ResetDefaultObject() const;

    friend class FClassRegistry;
    friend class FObjectInitializer;

    FName Name;
    const PClass* SuperClass = nullptr;
    std::size_t Size = 0;
    FConstructFunction Constructor = nullptr;
    const void* NativeTypeToken = nullptr;
    std::deque<PProperty> Properties;
    std::deque<PFunction> Functions;
    bool bMetadataValid = true;
    mutable bool bMetadataFinalized = false;
    EClassMetadataError MetadataError = EClassMetadataError::None;
    mutable FObjectPtr ClassDefaultObject;
    mutable std::vector<FDefaultSubobjectRecord> DefaultSubobjects;
};
}
