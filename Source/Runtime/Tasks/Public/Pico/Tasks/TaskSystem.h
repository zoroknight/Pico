#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Pico
{
enum class ETaskState
{
    Invalid,
    Queued,
    Running,
    Succeeded,
    Failed,
    Cancelled
};

struct FTaskSharedState;

class FCancellationToken
{
public:
    explicit FCancellationToken(std::weak_ptr<FTaskSharedState> State);
    bool IsCancellationRequested() const;

private:
    std::weak_ptr<FTaskSharedState> State;
};

class FTaskHandle
{
public:
    bool IsValid() const;
    std::uint64_t GetId() const;
    ETaskState GetState() const;
    std::string GetName() const;
    std::string GetError() const;
    bool IsComplete() const;
    bool RequestCancel() const;
    bool Wait(std::chrono::milliseconds Timeout) const;

private:
    explicit FTaskHandle(std::shared_ptr<FTaskSharedState> State);

    std::shared_ptr<FTaskSharedState> State;

    friend class FTaskSystem;
};

class FTaskSystem
{
public:
    using FTaskFunction = std::function<void(const FCancellationToken&)>;

    FTaskSystem();
    ~FTaskSystem();

    FTaskSystem(const FTaskSystem&) = delete;
    FTaskSystem& operator=(const FTaskSystem&) = delete;

    bool Initialize(std::size_t WorkerCount = 0);
    void Shutdown();
    FTaskHandle Submit(std::string Name, FTaskFunction Function);

    bool IsRunning() const;
    bool IsAcceptingTasks() const;
    std::size_t GetWorkerCount() const;
    std::size_t GetQueuedTaskCount() const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};

bool IsTaskComplete(ETaskState State);
std::string_view ToString(ETaskState State);
}
