#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/Property.h"

#include <cstddef>
#include <deque>
#include <span>
#include <vector>

namespace Pico
{
class PObject;
struct FObjectConstructionParams;

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
    bool AddProperty(PProperty Property);
    bool AddProperties(std::vector<PProperty> InProperties);
    bool IsMetadataValid() const;
    bool IsMetadataFinalized() const;
    const PProperty* FindProperty(FName PropertyName) const;
    const std::deque<PProperty>& GetProperties() const;

private:
    PClass(
        FName InName,
        const PClass* InSuperClass,
        std::size_t InSize,
        FConstructFunction InConstructor,
        const void* InNativeTypeToken);

    bool ValidateProperty(const PProperty& Property, std::span<const PProperty> PendingProperties) const;
    bool FinalizeMetadata() const;

    friend class FClassRegistry;

    FName Name;
    const PClass* SuperClass = nullptr;
    std::size_t Size = 0;
    FConstructFunction Constructor = nullptr;
    const void* NativeTypeToken = nullptr;
    std::deque<PProperty> Properties;
    bool bMetadataValid = true;
    mutable bool bMetadataFinalized = false;
};
}
