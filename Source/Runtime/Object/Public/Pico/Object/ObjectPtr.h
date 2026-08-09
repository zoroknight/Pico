#pragma once

#include "Pico/Object/ObjectTypes.h"

#include <cstddef>
#include <type_traits>

namespace Pico
{
template <typename TObject>
class TObjectPtr
{
public:
    TObjectPtr() = default;
    TObjectPtr(std::nullptr_t) {}
    TObjectPtr(TObject* Object) { Assign(Object); }

    TObjectPtr& operator=(TObject* Object)
    {
        Assign(Object);
        return *this;
    }

    TObjectPtr& operator=(std::nullptr_t)
    {
        Handle = {};
        return *this;
    }

    TObject* Get() const
    {
        return static_cast<TObject*>(ResolveObject(Handle));
    }

    TObject* operator->() const { return Get(); }
    explicit operator bool() const { return Get() != nullptr; }
    FObjectHandle GetHandle() const { return Handle; }
    void Reset() { Handle = {}; }

private:
    void Assign(TObject* Object)
    {
        Handle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    }

    FObjectHandle Handle;
};

template <typename TObject>
class TWeakObjectPtr
{
public:
    TWeakObjectPtr() = default;
    TWeakObjectPtr(std::nullptr_t) {}
    TWeakObjectPtr(TObject* Object) { Assign(Object); }
    TWeakObjectPtr(const TObjectPtr<TObject>& Object) : Handle(Object.GetHandle()) {}

    TWeakObjectPtr& operator=(TObject* Object)
    {
        Assign(Object);
        return *this;
    }

    TWeakObjectPtr& operator=(std::nullptr_t)
    {
        Handle = {};
        return *this;
    }

    TObject* Get() const
    {
        return static_cast<TObject*>(ResolveObject(Handle));
    }

    TObject* operator->() const { return Get(); }
    explicit operator bool() const { return Get() != nullptr; }
    FObjectHandle GetHandle() const { return Handle; }
    void Reset() { Handle = {}; }

private:
    void Assign(TObject* Object)
    {
        Handle = Object != nullptr ? Object->GetHandle() : FObjectHandle {};
    }

    FObjectHandle Handle;
};

template <typename T>
struct TObjectPointerTraits
{
    static constexpr bool IsObjectPointer = false;
    static constexpr bool IsStrong = false;
};

template <typename T>
struct TObjectPointerTraits<TObjectPtr<T>>
{
    using ObjectType = T;
    static constexpr bool IsObjectPointer = true;
    static constexpr bool IsStrong = true;
};

template <typename T>
struct TObjectPointerTraits<TWeakObjectPtr<T>>
{
    using ObjectType = T;
    static constexpr bool IsObjectPointer = true;
    static constexpr bool IsStrong = false;
};

static_assert(sizeof(TObjectPtr<PObject>) == sizeof(FObjectHandle));
static_assert(sizeof(TWeakObjectPtr<PObject>) == sizeof(FObjectHandle));
}
