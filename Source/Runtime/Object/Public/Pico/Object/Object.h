#pragma once

#include "Pico/Core/Delegate.h"
#include "Pico/Core/Name.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/PropertyChange.h"

#include <cstddef>
#include <span>
#include <string>

namespace Pico
{
class FArchive;
class FObjectRegistry;
class FObjectInitializer;
class FReferenceCollector;
class FWorldAssetLoader;
class PClass;
class PObject;
class PProperty;
enum class EObjectSerializationError;

using FObjectPropertyChangingDelegate =
    TMulticastDelegate<void(PObject*, const FPropertyChangedEvent&)>;
using FObjectPropertyChangedDelegate =
    TMulticastDelegate<void(PObject*, const FPropertyChangedEvent&)>;

PObject* LoadObject(FArchive& Archive, PObject* Outer, EObjectSerializationError* OutError);

struct FObjectConstructionParams
{
    const PClass* Class = nullptr;
    PObject* Outer = nullptr;
    FName Name;
    EObjectFlags Flags = EObjectFlags::None;
    const PObject* Template = nullptr;
};

class PObject
{
public:
    static const PClass* StaticClass();

    const PClass* GetClass() const;
    PObject* GetOuter() const;
    FName GetName() const;
    EObjectFlags GetFlags() const;
    FObjectHandle GetHandle() const;
    std::string GetPathName() const;
    bool IsA(const PClass* Class) const;
    bool IsBeginningDestroy() const;
    EFunctionInvokeResult ProcessEvent(
        const PFunction* Function,
        std::span<const FFunctionValue> Arguments = {},
        FFunctionValue* OutReturnValue = nullptr);
    FObjectPropertyChangingDelegate& OnPropertyChanging();
    FObjectPropertyChangedDelegate& OnPropertyChanged();
    const FObjectPropertyChangingDelegate& OnPropertyChanging() const;
    const FObjectPropertyChangedDelegate& OnPropertyChanged() const;
    virtual void PreEditChange(const PProperty* Property);
    virtual void PostEditChangeProperty(const FPropertyChangedEvent& Event);

protected:
    static void* operator new(std::size_t Size);
    static void operator delete(void* Memory) noexcept;

    explicit PObject(const FObjectConstructionParams& Params);
    virtual ~PObject() = default;
    virtual void PostInitProperties();
    virtual void PostLoad();
    virtual void BeginDestroy();
    virtual void AddReferencedObjects(FReferenceCollector& Collector) const;
    virtual bool DefineDefaultSubobjects(FObjectInitializer& Initializer);
    virtual bool OnDefaultSubobjectCreated(PObject* Subobject);
    virtual bool OnDefaultSubobjectRelation(
        PObject* Subobject,
        PObject* AttachParent,
        FName AttachSocketName,
        bool bIsRoot);

private:
    enum class ELifecycleState
    {
        Alive,
        BeginningDestroy,
        Destroying
    };

    static FObjectPtr ConstructInstance(const FObjectConstructionParams& Params);

    friend class FObjectRegistry;
    friend class FObjectInitializer;
    friend class PClass;
    friend class PProperty;
    friend class FWorldAssetLoader;
    friend struct FObjectDeleter;
    friend PObject* LoadObject(FArchive& Archive, PObject* Outer, EObjectSerializationError* OutError);

    void NotifyPrePropertyChange(const FPropertyChangedEvent& Event);
    void NotifyPostPropertyChange(const FPropertyChangedEvent& Event);

    const PClass* ClassPrivate = nullptr;
    PObject* OuterPrivate = nullptr;
    FName NamePrivate;
    EObjectFlags FlagsPrivate = EObjectFlags::None;
    FObjectHandle HandlePrivate;
    ELifecycleState LifecycleState = ELifecycleState::Alive;
    FObjectPropertyChangingDelegate PropertyChangingDelegate;
    FObjectPropertyChangedDelegate PropertyChangedDelegate;
};
}
