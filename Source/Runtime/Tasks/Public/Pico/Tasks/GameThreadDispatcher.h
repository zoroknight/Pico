#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Pico
{
struct FGameThreadDispatchBudget
{
    std::size_t MaxCallbacks = 64;
    std::chrono::microseconds MaxTime = std::chrono::milliseconds(2);
};

struct FGameThreadDispatchResult
{
    std::size_t ExecutedCallbacks = 0;
    std::size_t FailedCallbacks = 0;
    std::size_t RemainingCallbacks = 0;
};

class FGameThreadDispatcher
{
public:
    using FCallback = std::function<void()>;

    FGameThreadDispatcher();
    ~FGameThreadDispatcher();

    FGameThreadDispatcher(const FGameThreadDispatcher&) = delete;
    FGameThreadDispatcher& operator=(const FGameThreadDispatcher&) = delete;

    std::uint64_t Post(std::string Name, FCallback Callback);
    FGameThreadDispatchResult Pump(
        const FGameThreadDispatchBudget& Budget = {});
    void Shutdown();

    bool IsAcceptingCallbacks() const;
    std::size_t GetPendingCallbackCount() const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
