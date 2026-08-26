#pragma once

#include "Pico/Object/Object.h"

#include <string_view>
#include <type_traits>

namespace Pico
{
class PClass;
class PObject;

class FObjectInitializer
{
public:
    FObjectInitializer(PObject* InObject, const PObject* InTemplate);

    PObject* GetObject() const;
    const PObject* GetTemplate() const;
    bool InitializeProperties() const;

    PObject* CreateDefaultSubobject(const PClass* Class, FName Name);
    bool RemoveDefaultSubobject(FName Name);

    template <typename TObject>
    TObject* CreateDefaultSubobject(FName Name)
    {
        static_assert(
            std::is_base_of_v<PObject, TObject>,
            "Default subobjects must derive from PObject");
        return static_cast<TObject*>(CreateDefaultSubobject(TObject::StaticClass(), Name));
    }

    template <typename TObject>
    TObject* CreateDefaultSubobject(std::string_view Name)
    {
        return CreateDefaultSubobject<TObject>(FName(Name));
    }

    bool SetRootSubobject(PObject* Subobject);
    bool AttachSubobject(
        PObject* Subobject,
        PObject* AttachParent,
        FName SocketName = {});

private:
    bool InitializeClassDefaultObject(PClass& Class) const;
    bool InitializeDefaultSubobjectInstances() const;

    friend class PClass;
    friend PObject* NewObject(const FObjectConstructionParams& Params);

    PObject* Object = nullptr;
    const PObject* Template = nullptr;
};
}
