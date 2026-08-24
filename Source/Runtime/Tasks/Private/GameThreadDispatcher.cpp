#include "Pico/Tasks/GameThreadDispatcher.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Core/Log.h"

#include <chrono>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>

namespace Pico
{
struct FGameThreadDispatcher::FImpl
{
    struct FPendingCallback
    {
        std::uint64_t Id = 0;
        std::string Name;
        FCallback Callback;
    };

    mutable std::mutex Mutex;
    std::deque<FPendingCallback> Queue;
    std::uint64_t NextId = 1;
    bool bAcceptingCallbacks = true;
};

FGameThreadDispatcher::FGameThreadDispatcher()
    : Impl(std::make_unique<FImpl>())
{
}

FGameThreadDispatcher::~FGameThreadDispatcher()
{
    Shutdown();
}

std::uint64_t FGameThreadDispatcher::Post(
    std::string Name,
    FCallback Callback)
{
    if (!Callback)
    {
        return 0;
    }
    std::scoped_lock Lock(Impl->Mutex);
    if (!Impl->bAcceptingCallbacks)
    {
        return 0;
    }
    const std::uint64_t Id = Impl->NextId++;
    Impl->Queue.push_back({Id, std::move(Name), std::move(Callback)});
    return Id;
}

FGameThreadDispatchResult FGameThreadDispatcher::Pump(
    const FGameThreadDispatchBudget& Budget)
{
    FGameThreadDispatchResult Result;
    if (!CheckGameThread("FGameThreadDispatcher::Pump"))
    {
        Result.RemainingCallbacks = GetPendingCallbackCount();
        return Result;
    }

    const auto StartTime = std::chrono::steady_clock::now();
    while (Result.ExecutedCallbacks < Budget.MaxCallbacks)
    {
        FImpl::FPendingCallback Pending;
        {
            std::scoped_lock Lock(Impl->Mutex);
            if (Impl->Queue.empty())
            {
                break;
            }
            Pending = std::move(Impl->Queue.front());
            Impl->Queue.pop_front();
        }

        try
        {
            Pending.Callback();
        }
        catch (const std::exception& Exception)
        {
            ++Result.FailedCallbacks;
            PICO_LOG(
                LogCore,
                Error,
                "Game Thread callback '{}' failed: {}",
                Pending.Name,
                Exception.what());
        }
        catch (...)
        {
            ++Result.FailedCallbacks;
            PICO_LOG(
                LogCore,
                Error,
                "Game Thread callback '{}' failed with an unknown exception",
                Pending.Name);
        }
        ++Result.ExecutedCallbacks;

        if (Budget.MaxTime.count() > 0
            && std::chrono::steady_clock::now() - StartTime >= Budget.MaxTime)
        {
            break;
        }
    }
    Result.RemainingCallbacks = GetPendingCallbackCount();
    return Result;
}

void FGameThreadDispatcher::Shutdown()
{
    std::scoped_lock Lock(Impl->Mutex);
    Impl->bAcceptingCallbacks = false;
    Impl->Queue.clear();
}

bool FGameThreadDispatcher::IsAcceptingCallbacks() const
{
    std::scoped_lock Lock(Impl->Mutex);
    return Impl->bAcceptingCallbacks;
}

std::size_t FGameThreadDispatcher::GetPendingCallbackCount() const
{
    std::scoped_lock Lock(Impl->Mutex);
    return Impl->Queue.size();
}
}
