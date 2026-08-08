#pragma once

#include "Pico/Core/Delegate.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectGlobals.h"

#include <functional>
#include <type_traits>

namespace Pico
{
namespace Detail
{
inline bool IsDelegateObjectLive(FObjectHandle Handle)
{
    PObject* Object = ResolveObject(Handle);
    return Object != nullptr && !Object->IsBeginningDestroy();
}
}

template <typename TSignature>
class TObjectDelegate;

template <typename TResult, typename... TArgs>
class TObjectDelegate<TResult(TArgs...)> : public TDelegate<TResult(TArgs...)>
{
public:
    template <typename TObject>
    bool BindObject(TObject* Object, TResult (TObject::*Method)(TArgs...))
    {
        static_assert(std::is_base_of_v<PObject, TObject>);
        return BindObjectInternal(Object, Method);
    }

    template <typename TObject>
    bool BindObject(TObject* Object, TResult (TObject::*Method)(TArgs...) const)
    {
        static_assert(std::is_base_of_v<PObject, TObject>);
        return BindObjectInternal(Object, Method);
    }

private:
    template <typename TObject, typename TMethod>
    bool BindObjectInternal(TObject* Object, TMethod Method)
    {
        if (Object == nullptr
            || Method == nullptr
            || !Detail::IsDelegateObjectLive(Object->GetHandle()))
        {
            this->Unbind();
            return false;
        }

        const FObjectHandle Handle = Object->GetHandle();
        this->BindGuarded(
            Object,
            [Handle]()
            {
                return Detail::IsDelegateObjectLive(Handle);
            },
            [Handle, Method](TArgs... Arguments) -> TResult
            {
                PObject* Resolved = ResolveObject(Handle);
                if (Resolved == nullptr || Resolved->IsBeginningDestroy())
                {
                    throw std::bad_function_call();
                }
                if constexpr (std::is_void_v<TResult>)
                {
                    std::invoke(
                        Method,
                        static_cast<TObject*>(Resolved),
                        Arguments...);
                }
                else
                {
                    return std::invoke(
                        Method,
                        static_cast<TObject*>(Resolved),
                        Arguments...);
                }
            });
        return true;
    }
};

template <typename TSignature>
class TObjectMulticastDelegate;

template <typename... TArgs>
class TObjectMulticastDelegate<void(TArgs...)>
    : public TMulticastDelegate<void(TArgs...)>
{
public:
    template <typename TObject>
    FDelegateHandle AddObject(TObject* Object, void (TObject::*Method)(TArgs...))
    {
        static_assert(std::is_base_of_v<PObject, TObject>);
        return AddObjectInternal(Object, Method);
    }

    template <typename TObject>
    FDelegateHandle AddObject(TObject* Object, void (TObject::*Method)(TArgs...) const)
    {
        static_assert(std::is_base_of_v<PObject, TObject>);
        return AddObjectInternal(Object, Method);
    }

    std::size_t RemoveAll(PObject* Object)
    {
        return TMulticastDelegate<void(TArgs...)>::RemoveAll(Object);
    }

private:
    template <typename TObject, typename TMethod>
    FDelegateHandle AddObjectInternal(TObject* Object, TMethod Method)
    {
        if (Object == nullptr
            || Method == nullptr
            || !Detail::IsDelegateObjectLive(Object->GetHandle()))
        {
            return {};
        }

        const FObjectHandle Handle = Object->GetHandle();
        return this->AddGuarded(
            Object,
            [Handle]()
            {
                return Detail::IsDelegateObjectLive(Handle);
            },
            [Handle, Method](TArgs... Arguments)
            {
                PObject* Resolved = ResolveObject(Handle);
                if (Resolved != nullptr && !Resolved->IsBeginningDestroy())
                {
                    std::invoke(
                        Method,
                        static_cast<TObject*>(Resolved),
                        Arguments...);
                }
            });
    }
};
}
