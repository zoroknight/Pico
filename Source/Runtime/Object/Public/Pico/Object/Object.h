#pragma once

#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"

#include <cstddef>
#include <string>

namespace Pico
{
class FArchive;
class FObjectRegistry;
class FWorldAssetLoader;
class PClass;
class PObject;
class PProperty;
enum class EObjectSerializationError;

enum class EPropertyChangeType
{
    ValueSet,
    Interactive
};

struct FPropertyChangedEvent
{
    const PProperty* Property = nullptr;
    EPropertyChangeType ChangeType = EPropertyChangeType::ValueSet;
};

PObject* LoadObject(FArchive& Archive, PObject* Outer, EObjectSerializationError* OutError);

struct FObjectConstructionParams
{
    const PClass* Class = nullptr;
    PObject* Outer = nullptr;
    FName Name;
    EObjectFlags Flags = EObjectFlags::None;
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
    virtual void PostEditChangeProperty(const FPropertyChangedEvent& Event);

protected:
    static void* operator new(std::size_t Size);
    static void operator delete(void* Memory) noexcept;

    explicit PObject(const FObjectConstructionParams& Params);
    virtual ~PObject() = default;
    virtual void PostInitProperties();
    virtual void PostLoad();
    virtual void BeginDestroy();

private:
    enum class ELifecycleState
    {
        Alive,
        BeginningDestroy,
        Destroying
    };

    static FObjectPtr ConstructInstance(const FObjectConstructionParams& Params);

    friend class FObjectRegistry;
    friend class FWorldAssetLoader;
    friend struct FObjectDeleter;
    friend PObject* LoadObject(FArchive& Archive, PObject* Outer, EObjectSerializationError* OutError);

    const PClass* ClassPrivate = nullptr;
    PObject* OuterPrivate = nullptr;
    FName NamePrivate;
    EObjectFlags FlagsPrivate = EObjectFlags::None;
    FObjectHandle HandlePrivate;
    ELifecycleState LifecycleState = ELifecycleState::Alive;
};
}
