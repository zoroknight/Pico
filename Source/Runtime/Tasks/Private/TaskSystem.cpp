#include "Pico/Tasks/TaskSystem.h"

#include "Pico/Core/Log.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Pico
{
struct FTaskSharedState
{
    std::uint64_t Id = 0;
    std::string Name;
    std::atomic<ETaskState> State {ETaskState::Queued};
    std::atomic<bool> bCancellationRequested {false};
    mutable std::mutex CompletionMutex;
    std::condition_variable CompletionCondition;
    mutable std::mutex ErrorMutex;
    std::string Error;
};

namespace
{
void NotifyCompletion(const std::shared_ptr<FTaskSharedState>& State)
{
    State->CompletionCondition.notify_all();
}

bool RequestCancellation(const std::shared_ptr<FTaskSharedState>& State)
{
    if (State == nullptr || IsTaskComplete(State->State.load()))
    {
        return false;
    }

    State->bCancellationRequested.store(true);
    ETaskState Expected = ETaskState::Queued;
    if (State->State.compare_exchange_strong(Expected, ETaskState::Cancelled))
    {
        NotifyCompletion(State);
    }
    return true;
}
}

struct FTaskSystem::FImpl
{
    struct FQueuedTask
    {
        std::shared_ptr<FTaskSharedState> State;
        FTaskFunction Function;
    };

    mutable std::mutex Mutex;
    std::condition_variable WorkCondition;
    std::deque<FQueuedTask> Queue;
    std::vector<std::weak_ptr<FTaskSharedState>> Tasks;
    std::vector<std::thread> Workers;
    std::atomic<std::uint64_t> NextTaskId {1};
    bool bRunning = false;
    bool bAcceptingTasks = false;
    bool bStopping = false;

    void WorkerMain()
    {
        for (;;)
        {
            FQueuedTask Task;
            {
                std::unique_lock Lock(Mutex);
                WorkCondition.wait(
                    Lock,
                    [this]() { return bStopping || !Queue.empty(); });
                if (bStopping && Queue.empty())
                {
                    return;
                }
                Task = std::move(Queue.front());
                Queue.pop_front();
            }

            ETaskState Expected = ETaskState::Queued;
            if (!Task.State->State.compare_exchange_strong(
                    Expected, ETaskState::Running))
            {
                continue;
            }

            try
            {
                Task.Function(FCancellationToken(Task.State));
                Task.State->State.store(
                    Task.State->bCancellationRequested.load()
                        ? ETaskState::Cancelled
                        : ETaskState::Succeeded);
            }
            catch (const std::exception& Exception)
            {
                {
                    std::scoped_lock ErrorLock(Task.State->ErrorMutex);
                    Task.State->Error = Exception.what();
                }
                Task.State->State.store(ETaskState::Failed);
            }
            catch (...)
            {
                {
                    std::scoped_lock ErrorLock(Task.State->ErrorMutex);
                    Task.State->Error = "Unknown task exception";
                }
                Task.State->State.store(ETaskState::Failed);
            }
            NotifyCompletion(Task.State);
        }
    }
};

FCancellationToken::FCancellationToken(std::weak_ptr<FTaskSharedState> InState)
    : State(std::move(InState))
{
}

FCancellationToken::FCancellationToken(std::function<bool()> InCancellationQuery)
    : CancellationQuery(std::move(InCancellationQuery))
{
}

bool FCancellationToken::IsCancellationRequested() const
{
    if (CancellationQuery) return CancellationQuery();
    const std::shared_ptr<FTaskSharedState> Pinned = State.lock();
    return Pinned == nullptr || Pinned->bCancellationRequested.load();
}

FTaskHandle::FTaskHandle(std::shared_ptr<FTaskSharedState> InState)
    : State(std::move(InState))
{
}

bool FTaskHandle::IsValid() const
{
    return State != nullptr;
}

std::uint64_t FTaskHandle::GetId() const
{
    return State != nullptr ? State->Id : 0;
}

ETaskState FTaskHandle::GetState() const
{
    return State != nullptr ? State->State.load() : ETaskState::Invalid;
}

std::string FTaskHandle::GetName() const
{
    return State != nullptr ? State->Name : std::string {};
}

std::string FTaskHandle::GetError() const
{
    if (State == nullptr)
    {
        return {};
    }
    std::scoped_lock Lock(State->ErrorMutex);
    return State->Error;
}

bool FTaskHandle::IsComplete() const
{
    return IsTaskComplete(GetState());
}

bool FTaskHandle::RequestCancel() const
{
    return RequestCancellation(State);
}

bool FTaskHandle::Wait(std::chrono::milliseconds Timeout) const
{
    if (State == nullptr)
    {
        return false;
    }
    std::unique_lock Lock(State->CompletionMutex);
    return State->CompletionCondition.wait_for(
        Lock,
        Timeout,
        [this]() { return IsTaskComplete(State->State.load()); });
}

FTaskSystem::FTaskSystem()
    : Impl(std::make_unique<FImpl>())
{
}

FTaskSystem::~FTaskSystem()
{
    Shutdown();
}

bool FTaskSystem::Initialize(std::size_t WorkerCount)
{
    std::unique_lock Lock(Impl->Mutex);
    if (Impl->bRunning)
    {
        return false;
    }
    if (WorkerCount == 0)
    {
        const unsigned int HardwareThreads = std::thread::hardware_concurrency();
        WorkerCount = HardwareThreads > 1 ? HardwareThreads - 1 : 1;
        WorkerCount = std::clamp<std::size_t>(WorkerCount, 1, 4);
    }

    Impl->bStopping = false;
    Impl->bRunning = true;
    Impl->bAcceptingTasks = true;
    try
    {
        Impl->Workers.reserve(WorkerCount);
        for (std::size_t Index = 0; Index < WorkerCount; ++Index)
        {
            Impl->Workers.emplace_back([this]() { Impl->WorkerMain(); });
        }
    }
    catch (...)
    {
        Impl->bStopping = true;
        Impl->bRunning = false;
        Impl->bAcceptingTasks = false;
        Lock.unlock();
        Impl->WorkCondition.notify_all();
        for (std::thread& Worker : Impl->Workers)
        {
            if (Worker.joinable()) Worker.join();
        }
        Impl->Workers.clear();
        return false;
    }
    return true;
}

void FTaskSystem::Shutdown()
{
    std::vector<std::shared_ptr<FTaskSharedState>> TasksToCancel;
    {
        std::scoped_lock Lock(Impl->Mutex);
        if (!Impl->bRunning)
        {
            return;
        }
        Impl->bAcceptingTasks = false;
        Impl->bStopping = true;
        for (const std::weak_ptr<FTaskSharedState>& Task : Impl->Tasks)
        {
            if (const std::shared_ptr<FTaskSharedState> Pinned = Task.lock())
            {
                TasksToCancel.push_back(Pinned);
            }
        }
        Impl->Queue.clear();
    }

    for (const std::shared_ptr<FTaskSharedState>& Task : TasksToCancel)
    {
        RequestCancellation(Task);
    }
    Impl->WorkCondition.notify_all();
    for (std::thread& Worker : Impl->Workers)
    {
        if (Worker.joinable()) Worker.join();
    }

    {
        std::scoped_lock Lock(Impl->Mutex);
        Impl->Workers.clear();
        Impl->Tasks.clear();
        Impl->bRunning = false;
        Impl->bStopping = false;
    }
}

FTaskHandle FTaskSystem::Submit(std::string Name, FTaskFunction Function)
{
    if (!Function)
    {
        return FTaskHandle(nullptr);
    }

    std::shared_ptr<FTaskSharedState> State = std::make_shared<FTaskSharedState>();
    State->Id = Impl->NextTaskId.fetch_add(1);
    State->Name = std::move(Name);
    {
        std::scoped_lock Lock(Impl->Mutex);
        if (!Impl->bAcceptingTasks)
        {
            return FTaskHandle(nullptr);
        }
        Impl->Tasks.emplace_back(State);
        Impl->Queue.push_back({State, std::move(Function)});
    }
    Impl->WorkCondition.notify_one();
    return FTaskHandle(std::move(State));
}

bool FTaskSystem::IsRunning() const
{
    std::scoped_lock Lock(Impl->Mutex);
    return Impl->bRunning;
}

bool FTaskSystem::IsAcceptingTasks() const
{
    std::scoped_lock Lock(Impl->Mutex);
    return Impl->bAcceptingTasks;
}

std::size_t FTaskSystem::GetWorkerCount() const
{
    std::scoped_lock Lock(Impl->Mutex);
    return Impl->Workers.size();
}

std::size_t FTaskSystem::GetQueuedTaskCount() const
{
    std::scoped_lock Lock(Impl->Mutex);
    return Impl->Queue.size();
}

bool IsTaskComplete(ETaskState State)
{
    return State == ETaskState::Succeeded
        || State == ETaskState::Failed
        || State == ETaskState::Cancelled;
}

std::string_view ToString(ETaskState State)
{
    switch (State)
    {
    case ETaskState::Invalid: return "Invalid";
    case ETaskState::Queued: return "Queued";
    case ETaskState::Running: return "Running";
    case ETaskState::Succeeded: return "Succeeded";
    case ETaskState::Failed: return "Failed";
    case ETaskState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
}
