#include "Pico/Editor/EditorAgentExecutionService.h"

#include "Pico/Tasks/GameThreadDispatcher.h"
#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

bool IsCancelled(const FCancellationToken* Token)
{
    return Token && Token->IsCancellationRequested();
}

FAgentToolResult FailureResult(
    const FAgentToolCall& Call,
    std::string Message)
{
    FAgentToolResult Result;
    Result.CallId = Call.Id;
    Result.Error = std::move(Message);
    Result.OutputJson = "{}";
    return Result;
}
}

struct FEditorAgentExecutionService::FImpl
{
    struct FDispatchContext
    {
        mutable std::mutex Mutex;
        IAgentToolExecutor* EditorTools = nullptr;
        bool bShuttingDown = false;
    };

    struct FDispatchResult
    {
        std::mutex Mutex;
        std::condition_variable Condition;
        FAgentToolResult Result;
        std::string TraceJson = "[]";
        bool bStarted = false;
        bool bDone = false;
        bool bAbandoned = false;
    };

    FImpl(
        IAgentToolExecutor* InEditorTools,
        FGameThreadDispatcher* InDispatcher,
        std::filesystem::path OperationDirectory,
        FAsyncCompletion InAsyncCompletion)
        : Dispatcher(InDispatcher)
        , Journal(std::move(OperationDirectory))
        , AsyncCompletion(std::move(InAsyncCompletion))
        , DispatchContext(std::make_shared<FDispatchContext>())
    {
        DispatchContext->EditorTools = InEditorTools;
    }

    IAgentToolExecutor* GetTools() const
    {
        std::lock_guard Lock(DispatchContext->Mutex);
        return DispatchContext->bShuttingDown
            ? nullptr : DispatchContext->EditorTools;
    }

    void SetLastTrace(std::string Trace)
    {
        std::lock_guard Lock(TraceMutex);
        LastTraceJson = Trace;
        LastTraceByThread[std::this_thread::get_id()] = std::move(Trace);
    }

    std::string GetLastTrace() const
    {
        std::lock_guard Lock(TraceMutex);
        const auto It = LastTraceByThread.find(std::this_thread::get_id());
        if (It != LastTraceByThread.end()) return It->second;
        return LastTraceJson;
    }

    static std::string FailureTrace(
        std::string_view Stage,
        std::string_view Message)
    {
        return FJson::array({{{"stage", Stage}, {"succeeded", false},
            {"message", Message}}}).dump();
    }

    std::string IntentError(const FAgentToolCall& Call) const
    {
        EAgentTurnIntent Intent = EAgentTurnIntent::General;
        {
            std::lock_guard Lock(ContextMutex);
            const auto It = ExecutionContexts.find(std::this_thread::get_id());
            if (It != ExecutionContexts.end())
            {
                Intent = It->second.Intent;
                if (It->second.bSkillRestricted
                    && !It->second.AllowedTools.contains(Call.Name))
                {
                    return "The active Pico Skill does not allow tool '"
                        + Call.Name + "'";
                }
            }
        }
        if (Intent == EAgentTurnIntent::Play
            && Call.Name == "editor.project.package")
        {
            return "This turn requests Play, not packaging. Do not package the project; call editor.play.start instead.";
        }
        if (Intent == EAgentTurnIntent::Package
            && Call.Name == "editor.play.start")
        {
            return "This turn requests packaging, not Play. Do not start a Play Session; call editor.project.package instead.";
        }
        return {};
    }

    FAgentToolCall MakeJournalCall(const FAgentToolCall& Call) const
    {
        FAgentToolCall JournalCall = Call;
        std::lock_guard Lock(ContextMutex);
        const auto ContextIt = ExecutionContexts.find(std::this_thread::get_id());
        std::string Scope;
        if (ContextIt != ExecutionContexts.end())
        {
            Scope = ContextIt->second.SessionId;
            if (!ContextIt->second.RunId.empty())
            {
                if (!Scope.empty()) Scope += "/";
                Scope += ContextIt->second.RunId;
            }
        }
        if (Scope.empty()) Scope = DefaultSessionId;
        if (!Scope.empty()) JournalCall.Id = Scope + "/" + Call.Id;
        return JournalCall;
    }

    void DispatchRunLifecycle(
        std::string RunId,
        EAgentStatus Status,
        bool bBegin)
    {
        struct FCompletion
        {
            std::mutex Mutex;
            std::condition_variable Condition;
            bool bDone = false;
        };
        auto Completion = std::make_shared<FCompletion>();
        const std::shared_ptr<FDispatchContext> Context = DispatchContext;
        if (!Dispatcher || Dispatcher->Post(
                bBegin ? "Begin Agent Run ChangeSet" : "End Agent Run ChangeSet",
                [Completion, Context, RunId = std::move(RunId), Status, bBegin]()
                {
                    IAgentToolExecutor* Tools = nullptr;
                    {
                        std::lock_guard ContextLock(Context->Mutex);
                        if (!Context->bShuttingDown) Tools = Context->EditorTools;
                    }
                    if (Tools)
                    {
                        if (bBegin) Tools->BeginRun(RunId);
                        else Tools->EndRun(RunId, Status);
                    }
                    {
                        std::lock_guard Lock(Completion->Mutex);
                        Completion->bDone = true;
                    }
                    Completion->Condition.notify_all();
                }) == 0)
            return;

        std::unique_lock Lock(Completion->Mutex);
        while (!Completion->bDone && !bShuttingDown.load())
            Completion->Condition.wait_for(Lock, std::chrono::milliseconds(10));
    }

    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken)
    {
        SetLastTrace("[]");
        if (bShuttingDown.load())
        {
            const std::string Error = "Editor Agent execution service is shutting down";
            SetLastTrace(FailureTrace("Execute", Error));
            return FailureResult(Call, Error);
        }
        if (IsCancelled(CancellationToken))
        {
            const std::string Error = "Editor Agent tool call was cancelled before dispatch";
            SetLastTrace(FailureTrace("Cancellation", Error));
            return FailureResult(Call, Error);
        }

        const std::string BlockedReason = IntentError(Call);
        if (!BlockedReason.empty())
        {
            SetLastTrace(FailureTrace("Intent", BlockedReason));
            return FailureResult(Call, BlockedReason);
        }

        IAgentToolExecutor* Tools = GetTools();
        if (!Tools || !Dispatcher)
        {
            const std::string Error = "Game Thread execution service is unavailable";
            SetLastTrace(FailureTrace("Execute", Error));
            return FailureResult(Call, Error);
        }

        const bool bDurable = !Tools->IsReadOnly(Call);
        const FAgentToolCall JournalCall = bDurable
            ? MakeJournalCall(Call) : Call;
        std::unique_lock<std::mutex> MutationLock(MutationMutex, std::defer_lock);
        if (bDurable)
        {
            while (!MutationLock.try_lock())
            {
                if (bShuttingDown.load() || IsCancelled(CancellationToken))
                {
                    const std::string Error = "Editor Agent mutation was cancelled while waiting for the execution lane";
                    SetLastTrace(FailureTrace("Cancellation", Error));
                    return FailureResult(Call, Error);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            std::string JournalError;
            if (auto Recovered = Journal.FindApplied(JournalCall, &JournalError))
            {
                Recovered->CallId = Call.Id;
                SetLastTrace(FJson::array({{{"stage", "Recovery"},
                    {"succeeded", true},
                    {"message", "Recovered the previously applied tool result; handler was not called again"}}}).dump());
                return *Recovered;
            }
            if (!JournalError.empty()
                || !Journal.Prepare(JournalCall, &JournalError)
                || !Journal.MarkExecuting(JournalCall, &JournalError))
            {
                SetLastTrace(FailureTrace("Journal", JournalError));
                return FailureResult(Call,
                    "Could not prepare durable operation: " + JournalError);
            }
        }

        auto Shared = std::make_shared<FDispatchResult>();
        const std::shared_ptr<FDispatchContext> Context = DispatchContext;
        const std::uint64_t DispatchId = Dispatcher->Post(
            "Execute Agent editor tool",
            [Shared, Context, Call]()
            {
                IAgentToolExecutor* DispatchTools = nullptr;
                {
                    std::lock_guard Lock(Shared->Mutex);
                    if (Shared->bAbandoned)
                    {
                        Shared->Result = FailureResult(Call,
                            "Editor Agent tool call was cancelled before Game Thread execution");
                        Shared->bDone = true;
                        Shared->Condition.notify_all();
                        return;
                    }
                    Shared->bStarted = true;
                }
                {
                    std::lock_guard ContextLock(Context->Mutex);
                    if (!Context->bShuttingDown)
                        DispatchTools = Context->EditorTools;
                }
                FAgentToolResult Result = DispatchTools
                    ? DispatchTools->Execute(Call, nullptr)
                    : FailureResult(Call,
                        "Editor Agent execution service stopped before Game Thread execution");
                const std::string Trace = DispatchTools
                    ? DispatchTools->GetLastExecutionTraceJson()
                    : FImpl::FailureTrace("Execute", Result.Error);
                {
                    std::lock_guard Lock(Shared->Mutex);
                    Shared->Result = std::move(Result);
                    Shared->TraceJson = Trace;
                    Shared->bDone = true;
                }
                Shared->Condition.notify_all();
            });
        if (DispatchId == 0)
        {
            const std::string Error = "Game Thread dispatcher is unavailable";
            SetLastTrace(FailureTrace("Execute", Error));
            return FailureResult(Call, Error);
        }

        std::unique_lock Lock(Shared->Mutex);
        while (!Shared->bDone)
        {
            if ((bShuttingDown.load() || IsCancelled(CancellationToken))
                && !Shared->bStarted)
            {
                Shared->bAbandoned = true;
                const std::string Error = bShuttingDown.load()
                    ? "Editor Agent execution service stopped before Game Thread execution"
                    : "Editor Agent tool call was cancelled before Game Thread execution";
                SetLastTrace(FailureTrace("Cancellation", Error));
                return FailureResult(Call, Error);
            }
            Shared->Condition.wait_for(Lock, std::chrono::milliseconds(10));
        }
        FAgentToolResult Result = Shared->Result;
        const std::string DispatchTrace = Shared->TraceJson;
        Lock.unlock();
        SetLastTrace(DispatchTrace);

        if (Result.bSucceeded && AsyncCompletion)
        {
            Result = AsyncCompletion(Call, std::move(Result), CancellationToken);
            if (!Result.bSucceeded)
                SetLastTrace(FailureTrace("AsyncCompletion",
                    Result.Error.empty()
                        ? "Asynchronous tool operation failed" : Result.Error));
        }
        if (bDurable)
        {
            std::string JournalError;
            if (!Journal.MarkApplied(JournalCall, Result, &JournalError))
            {
                SetLastTrace(FailureTrace("Journal", JournalError));
                return FailureResult(Call,
                    "Tool returned, but its durable result could not be recorded; outcome may be uncertain: "
                        + JournalError);
            }
        }
        return Result;
    }

    FGameThreadDispatcher* Dispatcher = nullptr;
    FAgentOperationJournal Journal;
    FAsyncCompletion AsyncCompletion;
    std::shared_ptr<FDispatchContext> DispatchContext;
    std::atomic<bool> bShuttingDown {false};
    struct FExecutionContext
    {
        std::string SessionId;
        std::string RunId;
        EAgentTurnIntent Intent = EAgentTurnIntent::General;
        std::unordered_set<std::string> AllowedTools;
        bool bSkillRestricted = false;
    };
    mutable std::mutex ContextMutex;
    std::string DefaultSessionId;
    std::unordered_map<std::thread::id, FExecutionContext> ExecutionContexts;
    std::mutex MutationMutex;
    mutable std::mutex TraceMutex;
    std::string LastTraceJson = "[]";
    std::unordered_map<std::thread::id, std::string> LastTraceByThread;
};

FEditorAgentExecutionService::FEditorAgentExecutionService(
    IAgentToolExecutor* EditorTools,
    FGameThreadDispatcher* Dispatcher,
    std::filesystem::path OperationDirectory,
    FAsyncCompletion AsyncCompletion)
    : Impl(std::make_unique<FImpl>(EditorTools, Dispatcher,
        std::move(OperationDirectory), std::move(AsyncCompletion)))
{
}

FEditorAgentExecutionService::~FEditorAgentExecutionService()
{
    Shutdown();
}

void FEditorAgentExecutionService::SetSessionId(std::string SessionId)
{
    if (!Impl) return;
    std::lock_guard Lock(Impl->ContextMutex);
    Impl->DefaultSessionId = SessionId;
    Impl->ExecutionContexts[std::this_thread::get_id()].SessionId =
        std::move(SessionId);
}

void FEditorAgentExecutionService::SetTurnIntent(EAgentTurnIntent Intent)
{
    if (!Impl) return;
    std::lock_guard Lock(Impl->ContextMutex);
    Impl->ExecutionContexts[std::this_thread::get_id()].Intent = Intent;
}

void FEditorAgentExecutionService::SetAllowedTools(
    const std::vector<FAgentSkill>& Skills)
{
    if (!Impl) return;
    std::lock_guard Lock(Impl->ContextMutex);
    FImpl::FExecutionContext& Context =
        Impl->ExecutionContexts[std::this_thread::get_id()];
    Context.bSkillRestricted = !Skills.empty();
    Context.AllowedTools.clear();
    for (const FAgentSkill& Skill : Skills)
        Context.AllowedTools.insert(
            Skill.AllowedTools.begin(), Skill.AllowedTools.end());
}

std::vector<FAgentOperationRecord>
FEditorAgentExecutionService::ListIncompleteOperations() const
{
    return Impl ? Impl->Journal.ListIncomplete()
        : std::vector<FAgentOperationRecord> {};
}

void FEditorAgentExecutionService::Shutdown()
{
    if (!Impl || Impl->bShuttingDown.exchange(true)) return;
    std::lock_guard Lock(Impl->DispatchContext->Mutex);
    Impl->DispatchContext->bShuttingDown = true;
    Impl->DispatchContext->EditorTools = nullptr;
}

void FEditorAgentExecutionService::BeginRun(std::string_view RunId)
{
    if (!Impl) return;
    {
        std::lock_guard Lock(Impl->ContextMutex);
        FImpl::FExecutionContext& Context =
            Impl->ExecutionContexts[std::this_thread::get_id()];
        if (Context.SessionId.empty()) Context.SessionId = Impl->DefaultSessionId;
        Context.RunId = RunId;
    }
    Impl->DispatchRunLifecycle(
        std::string(RunId), EAgentStatus::Planning, true);
}

void FEditorAgentExecutionService::EndRun(
    std::string_view RunId,
    EAgentStatus Status)
{
    if (!Impl) return;
    Impl->DispatchRunLifecycle(std::string(RunId), Status, false);
    std::lock_guard Lock(Impl->ContextMutex);
    const auto It = Impl->ExecutionContexts.find(std::this_thread::get_id());
    if (It != Impl->ExecutionContexts.end() && It->second.RunId == RunId)
        It->second.RunId.clear();
}

bool FEditorAgentExecutionService::RequiresApproval(
    const FAgentToolCall& Call) const
{
    if (!Impl || !Impl->IntentError(Call).empty()) return false;
    IAgentToolExecutor* Tools = Impl->GetTools();
    return Tools && Tools->RequiresApproval(Call);
}

bool FEditorAgentExecutionService::IsReadOnly(
    const FAgentToolCall& Call) const
{
    IAgentToolExecutor* Tools = Impl ? Impl->GetTools() : nullptr;
    return Tools && Tools->IsReadOnly(Call);
}

std::vector<std::string> FEditorAgentExecutionService::GetRevisionReadSet(
    const FAgentToolCall& Call) const
{
    IAgentToolExecutor* Tools = Impl ? Impl->GetTools() : nullptr;
    return Tools ? Tools->GetRevisionReadSet(Call)
        : std::vector<std::string> {"State.Revision"};
}

std::vector<std::string> FEditorAgentExecutionService::GetRevisionWriteSet(
    const FAgentToolCall& Call) const
{
    IAgentToolExecutor* Tools = Impl ? Impl->GetTools() : nullptr;
    return Tools ? Tools->GetRevisionWriteSet(Call)
        : std::vector<std::string> {"State.Revision"};
}

void FEditorAgentExecutionService::PrepareApproval(
    const FAgentToolCall& Call)
{
    if (!Impl || !Impl->IntentError(Call).empty()) return;
    if (IAgentToolExecutor* Tools = Impl->GetTools())
        Tools->PrepareApproval(Call);
}

bool FEditorAgentExecutionService::PrepareApprovalDecision(
    const FAgentToolCall& Call, bool bApproved)
{
    if (!Impl || !Impl->IntentError(Call).empty()) return false;
    IAgentToolExecutor* Tools = Impl->GetTools();
    return Tools && Tools->PrepareApprovalDecision(Call, bApproved);
}

FAgentToolResult FEditorAgentExecutionService::Execute(
    const FAgentToolCall& Call,
    const FCancellationToken* CancellationToken)
{
    return Impl ? Impl->Execute(Call, CancellationToken)
        : FailureResult(Call, "Editor Agent execution service is unavailable");
}

void FEditorAgentExecutionService::CommitDurableResult(
    const FAgentToolCall& Call)
{
    if (!Impl || IsReadOnly(Call)) return;
    std::lock_guard Lock(Impl->MutationMutex);
    std::string Error;
    Impl->Journal.MarkCommitted(Impl->MakeJournalCall(Call), &Error);
}

std::string FEditorAgentExecutionService::GetLastExecutionTraceJson() const
{
    return Impl ? Impl->GetLastTrace() : "[]";
}
}
