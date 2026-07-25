#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/Property.h"

#include <cstddef>
#include <deque>

namespace Pico
{
class PObject;
struct FObjectConstructionParams;

class PClass
{
public:
    using FConstructFunction = FObjectPtr (*)(const FObjectConstructionParams&);

    PClass(FName InName, const PClass* InSuperClass, std::size_t InSize, FConstructFunction InConstructor);

    FName GetName() const;
    const PClass* GetSuperClass() const;
    std::size_t GetSize() const;
    bool IsChildOf(const PClass* Other) const;
    bool CanConstruct() const;
    FObjectPtr ConstructObject(const FObjectConstructionParams& Params) const;
    bool AddProperty(PProperty Property);
    const PProperty* FindProperty(FName PropertyName) const;
    const std::deque<PProperty>& GetProperties() const;

private:
    FName Name;
    const PClass* SuperClass = nullptr;
    std::size_t Size = 0;
    FConstructFunction Constructor = nullptr;
    std::deque<PProperty> Properties;
};
}
