#include "TestRunner.h"

#include "Pico/Core/GameThread.h"
#include "Pico/Tasks/GameThreadDispatcher.h"
#include "Pico/Tasks/TaskSystem.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace
{
bool WaitUntil(const std::function<bool()>& Predicate)
{
    const auto Deadline = std::chrono::steady_clock::now() + 2s;
    while (!Predicate() && std::chrono::steady_clock::now() < Deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    return Predicate();
}

void TestTaskCompletionAndDispatch(FTestRunner& Runner)
{
    Pico::FTaskSystem Tasks;
    Pico::FGameThreadDispatcher Dispatcher;
    Runner.Expect(Tasks.Initialize(2), "Task system starts a fixed worker pool");

    std::atomic<bool> WorkerWasOffGameThread {false};
    bool CallbackWasOnGameThread = false;
    Pico::FTaskHandle Task = Tasks.Submit(
        "Fake async provider",
        [&](const Pico::FCancellationToken& Token)
        {
            WorkerWasOffGameThread.store(!Pico::IsInGameThread());
            if (!Token.IsCancellationRequested())
            {
                Dispatcher.Post(
                    "Apply fake provider result",
                    [&]() { CallbackWasOnGameThread = Pico::IsInGameThread(); });
            }
        });

    Runner.Expect(
        Task.IsValid() && Task.Wait(2s)
            && Task.GetState() == Pico::ETaskState::Succeeded,
        "A background task completes and exposes stable state");
    Runner.Expect(
        WorkerWasOffGameThread.load(),
        "Background task work does not run on the Game Thread");

    const Pico::FGameThreadDispatchResult DispatchResult = Dispatcher.Pump();
    Runner.Expect(
        DispatchResult.ExecutedCallbacks == 1 && CallbackWasOnGameThread,
        "Background results return through the Game Thread Dispatcher");
    Tasks.Shutdown();
}

void TestQueuedAndRunningCancellation(FTestRunner& Runner)
{
    Pico::FTaskSystem Tasks;
    Tasks.Initialize(1);
    std::atomic<bool> BlockerStarted {false};
    std::atomic<bool> ReleaseBlocker {false};
    Pico::FTaskHandle Blocker = Tasks.Submit(
        "Block worker",
        [&](const Pico::FCancellationToken& Token)
        {
            BlockerStarted.store(true);
            while (!ReleaseBlocker.load() && !Token.IsCancellationRequested())
            {
                std::this_thread::sleep_for(1ms);
            }
        });
    Runner.Expect(
        WaitUntil([&]() { return BlockerStarted.load(); }),
        "Worker starts the first task before queued cancellation test");

    std::atomic<bool> QueuedTaskRan {false};
    Pico::FTaskHandle Queued = Tasks.Submit(
        "Cancel while queued",
        [&](const Pico::FCancellationToken&) { QueuedTaskRan.store(true); });
    Runner.Expect(
        Queued.RequestCancel() && Queued.Wait(2s)
            && Queued.GetState() == Pico::ETaskState::Cancelled
            && !QueuedTaskRan.load(),
        "A queued task can be cancelled without executing");

    Runner.Expect(
        Blocker.RequestCancel() && Blocker.Wait(2s)
            && Blocker.GetState() == Pico::ETaskState::Cancelled,
        "A running cooperative task observes cancellation");
    ReleaseBlocker.store(true);
    Tasks.Shutdown();
}

void TestFailureAndSafeShutdown(FTestRunner& Runner)
{
    Pico::FTaskSystem Tasks;
    Tasks.Initialize(1);
    Pico::FTaskHandle Failed = Tasks.Submit(
        "Throwing task",
        [](const Pico::FCancellationToken&)
        {
            throw std::runtime_error("expected failure");
        });
    Runner.Expect(
        Failed.Wait(2s)
            && Failed.GetState() == Pico::ETaskState::Failed
            && Failed.GetError() == "expected failure",
        "Worker exceptions become inspectable task failures");

    std::atomic<bool> Started {false};
    Pico::FTaskHandle Running = Tasks.Submit(
        "Stop with system",
        [&](const Pico::FCancellationToken& Token)
        {
            Started.store(true);
            while (!Token.IsCancellationRequested())
            {
                std::this_thread::sleep_for(1ms);
            }
        });
    WaitUntil([&]() { return Started.load(); });
    Tasks.Shutdown();
    Runner.Expect(
        Running.IsComplete()
            && Running.GetState() == Pico::ETaskState::Cancelled
            && !Tasks.IsRunning()
            && !Tasks.IsAcceptingTasks(),
        "Shutdown cancels cooperative work and joins every worker");
    Runner.Expect(
        !Tasks.Submit("Rejected", [](const Pico::FCancellationToken&) {}).IsValid(),
        "Shutdown rejects new tasks");
}

void TestDispatcherBudgetAndShutdown(FTestRunner& Runner)
{
    Pico::FGameThreadDispatcher Dispatcher;
    int Total = 0;
    Dispatcher.Post("One", [&]() { ++Total; });
    Dispatcher.Post("Failure", []() { throw std::runtime_error("dispatch failure"); });
    Dispatcher.Post("Three", [&]() { ++Total; });

    Pico::FGameThreadDispatchBudget Budget;
    Budget.MaxCallbacks = 2;
    Budget.MaxTime = 1s;
    const Pico::FGameThreadDispatchResult First = Dispatcher.Pump(Budget);
    Runner.Expect(
        First.ExecutedCallbacks == 2
            && First.FailedCallbacks == 1
            && First.RemainingCallbacks == 1
            && Total == 1,
        "Dispatcher enforces callback budget and isolates callback exceptions");

    const Pico::FGameThreadDispatchResult Second = Dispatcher.Pump();
    Runner.Expect(
        Second.ExecutedCallbacks == 1
            && Second.RemainingCallbacks == 0
            && Total == 2,
        "A later frame drains callbacks left by the previous budget");

    Dispatcher.Post("Discarded", [&]() { ++Total; });
    Dispatcher.Shutdown();
    Runner.Expect(
        Dispatcher.GetPendingCallbackCount() == 0
            && Dispatcher.Post("Rejected", [&]() { ++Total; }) == 0,
        "Dispatcher shutdown discards pending work and rejects new callbacks");
}
}

int main()
{
    FTestRunner Runner;
    Runner.Expect(
        Pico::InitializeGameThread(),
        "Task tests register their main thread as the Game Thread");
    TestTaskCompletionAndDispatch(Runner);
    TestQueuedAndRunningCancellation(Runner);
    TestFailureAndSafeShutdown(Runner);
    TestDispatcherBudgetAndShutdown(Runner);
    Pico::ShutdownGameThread();
    return Runner.Finish();
}
