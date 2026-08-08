#pragma once

#include "Pico/Core/Types.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Pico
{
class FDelegateHandle
{
public:
    FDelegateHandle() = default;
    static FDelegateHandle Generate();

    bool IsValid() const;
    void Reset();

    friend bool operator==(const FDelegateHandle&, const FDelegateHandle&) = default;

private:
    explicit FDelegateHandle(uint64 InId);

    uint64 Id = 0;
};

template <typename TSignature>
class TDelegate;

namespace Detail
{
template <typename TResult>
struct TExecuteIfBoundResult
{
    using Type = std::optional<TResult>;
};

template <>
struct TExecuteIfBoundResult<void>
{
    using Type = bool;
};
}

template <typename TResult, typename... TArgs>
class TDelegate<TResult(TArgs...)>
{
public:
    using FCallback = std::function<TResult(TArgs...)>;
    using FGuard = std::function<bool()>;

    template <typename TCallable>
    void BindLambda(TCallable&& Callable)
    {
        static_assert(
            std::is_invocable_r_v<TResult, TCallable&, TArgs...>,
            "Delegate callback signature does not match");
        Callback = std::forward<TCallable>(Callable);
        Guard = {};
        OwnerKey = nullptr;
    }

    void BindStatic(TResult (*Function)(TArgs...))
    {
        BindLambda(Function);
    }

    template <typename TCallable, typename TGuard>
    void BindGuarded(
        const void* InOwnerKey,
        TGuard&& InGuard,
        TCallable&& Callable)
    {
        static_assert(
            std::is_invocable_r_v<TResult, TCallable&, TArgs...>,
            "Delegate callback signature does not match");
        static_assert(
            std::is_invocable_r_v<bool, TGuard&>,
            "Delegate guard must return bool");
        Callback = std::forward<TCallable>(Callable);
        Guard = std::forward<TGuard>(InGuard);
        OwnerKey = InOwnerKey;
    }

    bool IsBound() const
    {
        return Callback != nullptr && (!Guard || Guard());
    }

    bool IsBoundTo(const void* InOwnerKey) const
    {
        return InOwnerKey != nullptr && OwnerKey == InOwnerKey && IsBound();
    }

    void Unbind()
    {
        Callback = {};
        Guard = {};
        OwnerKey = nullptr;
    }

    TResult Execute(TArgs... Arguments) const
    {
        if (!IsBound())
        {
            throw std::bad_function_call();
        }
        if constexpr (std::is_void_v<TResult>)
        {
            Callback(Arguments...);
        }
        else
        {
            return Callback(Arguments...);
        }
    }

    typename Detail::TExecuteIfBoundResult<TResult>::Type ExecuteIfBound(
        TArgs... Arguments) const
    {
        if (!IsBound())
        {
            if constexpr (std::is_void_v<TResult>)
            {
                return false;
            }
            else
            {
                return std::nullopt;
            }
        }
        if constexpr (std::is_void_v<TResult>)
        {
            Callback(Arguments...);
            return true;
        }
        else
        {
            return Callback(Arguments...);
        }
    }

private:
    FCallback Callback;
    FGuard Guard;
    const void* OwnerKey = nullptr;
};

template <typename TSignature>
class TMulticastDelegate;

template <typename... TArgs>
class TMulticastDelegate<void(TArgs...)>
{
public:
    using FCallback = std::function<void(TArgs...)>;
    using FGuard = std::function<bool()>;

    TMulticastDelegate() = default;
    TMulticastDelegate(const TMulticastDelegate&) = delete;
    TMulticastDelegate& operator=(const TMulticastDelegate&) = delete;
    TMulticastDelegate(TMulticastDelegate&&) = delete;
    TMulticastDelegate& operator=(TMulticastDelegate&&) = delete;

    template <typename TCallable>
    FDelegateHandle AddLambda(TCallable&& Callable)
    {
        return AddGuarded(
            nullptr,
            []() { return true; },
            std::forward<TCallable>(Callable));
    }

    FDelegateHandle AddStatic(void (*Function)(TArgs...))
    {
        return AddLambda(Function);
    }

    template <typename TCallable, typename TGuard>
    FDelegateHandle AddGuarded(
        const void* OwnerKey,
        TGuard&& Guard,
        TCallable&& Callable)
    {
        static_assert(
            std::is_invocable_r_v<void, TCallable&, TArgs...>,
            "Multicast delegate callback signature does not match");
        static_assert(
            std::is_invocable_r_v<bool, TGuard&>,
            "Multicast delegate guard must return bool");

        const FDelegateHandle Handle = FDelegateHandle::Generate();
        Bindings.push_back(FBinding {
            Handle,
            std::forward<TCallable>(Callable),
            std::forward<TGuard>(Guard),
            OwnerKey,
            true });
        return Handle;
    }

    bool Remove(FDelegateHandle Handle)
    {
        if (!Handle.IsValid())
        {
            return false;
        }
        FBinding* Binding = FindBinding(Handle);
        if (Binding == nullptr)
        {
            return false;
        }
        Binding->bActive = false;
        CompactIfIdle();
        return true;
    }

    std::size_t RemoveAll(const void* OwnerKey)
    {
        if (OwnerKey == nullptr)
        {
            return 0;
        }
        std::size_t RemovedCount = 0;
        for (FBinding& Binding : Bindings)
        {
            if (Binding.bActive && Binding.OwnerKey == OwnerKey)
            {
                Binding.bActive = false;
                ++RemovedCount;
            }
        }
        CompactIfIdle();
        return RemovedCount;
    }

    void Clear()
    {
        if (BroadcastDepth == 0)
        {
            Bindings.clear();
            return;
        }
        for (FBinding& Binding : Bindings)
        {
            Binding.bActive = false;
        }
    }

    bool IsBound() const
    {
        return Num() != 0;
    }

    bool IsBoundTo(const void* OwnerKey) const
    {
        if (OwnerKey == nullptr)
        {
            return false;
        }
        return std::any_of(
            Bindings.begin(),
            Bindings.end(),
            [OwnerKey](const FBinding& Binding)
            {
                return Binding.bActive
                    && Binding.OwnerKey == OwnerKey
                    && (!Binding.Guard || Binding.Guard());
            });
    }

    std::size_t Num() const
    {
        return static_cast<std::size_t>(std::count_if(
            Bindings.begin(),
            Bindings.end(),
            [](const FBinding& Binding)
            {
                return Binding.bActive
                    && (!Binding.Guard || Binding.Guard());
            }));
    }

    void Broadcast(TArgs... Arguments)
    {
        std::vector<FDelegateHandle> Snapshot;
        Snapshot.reserve(Bindings.size());
        for (const FBinding& Binding : Bindings)
        {
            if (Binding.bActive)
            {
                Snapshot.push_back(Binding.Handle);
            }
        }

        ++BroadcastDepth;
        try
        {
            for (const FDelegateHandle Handle : Snapshot)
            {
                FBinding* Binding = FindBinding(Handle);
                if (Binding == nullptr || !Binding->bActive)
                {
                    continue;
                }
                if (Binding->Guard && !Binding->Guard())
                {
                    Binding->bActive = false;
                    continue;
                }

                FCallback Callback = Binding->Callback;
                Callback(Arguments...);
            }
        }
        catch (...)
        {
            FinishBroadcast();
            throw;
        }
        FinishBroadcast();
    }

private:
    struct FBinding
    {
        FDelegateHandle Handle;
        FCallback Callback;
        FGuard Guard;
        const void* OwnerKey = nullptr;
        bool bActive = true;
    };

    FBinding* FindBinding(FDelegateHandle Handle)
    {
        const auto Found = std::find_if(
            Bindings.begin(),
            Bindings.end(),
            [Handle](const FBinding& Binding)
            {
                return Binding.Handle == Handle;
            });
        return Found != Bindings.end() ? &*Found : nullptr;
    }

    void FinishBroadcast()
    {
        if (BroadcastDepth > 0)
        {
            --BroadcastDepth;
        }
        CompactIfIdle();
    }

    void CompactIfIdle()
    {
        if (BroadcastDepth == 0)
        {
            std::erase_if(
                Bindings,
                [](const FBinding& Binding)
                {
                    return !Binding.bActive;
                });
        }
    }

    std::vector<FBinding> Bindings;
    std::size_t BroadcastDepth = 0;
};
}
