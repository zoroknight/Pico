#pragma once

#include <type_traits>
#include <utility>

namespace Pico
{
template <typename TCallback>
class TScopeExit final
{
public:
    explicit TScopeExit(TCallback&& InCallback)
        : Callback(std::forward<TCallback>(InCallback))
    {
    }

    TScopeExit(const TScopeExit&) = delete;
    TScopeExit& operator=(const TScopeExit&) = delete;

    TScopeExit(TScopeExit&& Other) noexcept(std::is_nothrow_move_constructible_v<TCallback>)
        : Callback(std::move(Other.Callback))
        , bActive(std::exchange(Other.bActive, false))
    {
    }

    ~TScopeExit()
    {
        if (bActive)
        {
            Callback();
        }
    }

    void Release()
    {
        bActive = false;
    }

private:
    TCallback Callback;
    bool bActive = true;
};

template <typename TCallback>
TScopeExit<std::decay_t<TCallback>> MakeScopeExit(TCallback&& Callback)
{
    return TScopeExit<std::decay_t<TCallback>>(std::forward<TCallback>(Callback));
}
}
