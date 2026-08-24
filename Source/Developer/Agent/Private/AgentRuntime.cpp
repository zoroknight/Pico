#include "Pico/Agent/AgentRuntime.h"

#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;
}

FAgentRuntime::FAgentRuntime(
    FAgentSession& InSession,
    IAgentProvider& InProvider,
    IAgentToolExecutor& InToolExecutor,
    FAgentBudget InBudget,
    FAgentRuntimeContext InContext)
    : Session(InSession)
    , Provider(InProvider)
    , ToolExecutor(InToolExecutor)
    , Budget(InBudget)
    , Context(std::move(InContext))
    , Counters(InSession.GetStatus() == EAgentStatus::Planning
            || InSession.GetStatus() == EAgentStatus::AwaitingApproval
            || InSession.GetStatus() == EAgentStatus::ExecutingTool
            || InSession.GetStatus() == EAgentStatus::Validating
            || InSession.GetStatus() == EAgentStatus::Repairing
        ? InSession.GetCounters()
        : FAgentCounters {})
{
}

FAgentRunResult FAgentRuntime::Run(
    std::string Prompt,
    const FCancellationToken* CancellationToken)
{
    StartTime = std::chrono::steady_clock::now();
    std::string Error;
    if (!Prompt.empty())
    {
        CurrentGoal = Prompt;
        FAgentEvent UserMessage;
        UserMessage.Type = EAgentEventType::Message;
        UserMessage.Role = EAgentRole::User;
        UserMessage.Content = std::move(Prompt);
        if (!Session.Append(std::move(UserMessage), &Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error));
        }
    }
    else if (CurrentGoal.empty())
    {
        const std::vector<FAgentMessage> History = Session.BuildMessageHistory();
        for (auto It = History.rbegin(); It != History.rend(); ++It)
        {
            if (It->Role == EAgentRole::User)
            {
                CurrentGoal = It->Content;
                break;
            }
        }
    }

    if (!Transition(EAgentStatus::Planning, Error))
    {
        return Finish(EAgentStatus::Failed, std::move(Error));
    }

    while (true)
    {
        if (IsCancelled(CancellationToken))
        {
            return Finish(EAgentStatus::Cancelled, "Agent run was cancelled");
        }
        if (!CheckBudget(Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error));
        }

        ++Counters.Steps;
        FAgentProviderRequest Request;
        Request.Messages = Session.BuildMessageHistory();
        Request.ProgressLedgerJson = BuildProgressLedgerJson();
        Request.KnowledgeContextJson = Context.KnowledgeContextJson;
        Request.SkillContextJson = Context.SkillContextJson;
        Request.OnTextDelta = Context.OnAssistantDelta;
        Request.Step = Counters.Steps;
        Request.RepairAttempt = Counters.RepairAttempts;
        FAgentProviderResponse Response = Provider.Generate(Request, CancellationToken);

        if (IsCancelled(CancellationToken) || Response.Error == "Cancelled")
        {
            return Finish(EAgentStatus::Cancelled, "Agent run was cancelled");
        }
        if (!Response.bSucceeded)
        {
            FAgentEvent Failure;
            Failure.Type = EAgentEventType::Error;
            Failure.Content = Response.Error.empty() ? "Provider failed" : Response.Error;
            Session.Append(std::move(Failure));
            if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
            {
                return Finish(EAgentStatus::Failed, "Agent repair budget exhausted");
            }
            ++Counters.RepairAttempts;
            if (!Transition(EAgentStatus::Repairing, Error)
                || !Session.WriteCheckpoint(EAgentStatus::Repairing, Counters, &Error)
                || !Transition(EAgentStatus::Planning, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
            continue;
        }

        if (!Response.Content.empty())
        {
            FAgentEvent AssistantMessage;
            AssistantMessage.Type = EAgentEventType::Message;
            AssistantMessage.Role = EAgentRole::Assistant;
            AssistantMessage.Content = Response.Content;
            if (!Session.Append(std::move(AssistantMessage), &Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
        }

        if (!Response.ToolCalls.empty())
        {
            if (Budget.ReservedFinalSteps > 0
                && Counters.Steps > Budget.MaxSteps - std::min(
                    Budget.MaxSteps, Budget.ReservedFinalSteps))
            {
                return Finish(EAgentStatus::Failed,
                    "Agent used the step reserved for its final answer to request another tool");
            }
            bool bNeedsApproval = false;
            std::size_t NewToolCallCount = 0;
            std::size_t NewReadOnlyCallCount = 0;
            std::size_t NewMutationCallCount = 0;
            for (const FAgentToolCall& Call : Response.ToolCalls)
            {
                const std::optional<FAgentToolResult> ExistingResult =
                    Session.FindToolResult(Call.Id);
                if (ExistingResult && !Session.MatchesToolCall(Call))
                {
                    return Finish(EAgentStatus::Failed,
                        "ToolCall id was reused with different tool or arguments: " + Call.Id);
                }
                const bool bReadOnly = ToolExecutor.IsReadOnly(Call);
                const bool bSemanticCacheHit = !ExistingResult && bReadOnly
                    && ReadOnlyCache.contains(MakeSemanticKey(Call));
                if (!ExistingResult && !bSemanticCacheHit)
                {
                    ++NewToolCallCount;
                    if (bReadOnly) ++NewReadOnlyCallCount;
                    else ++NewMutationCallCount;
                }
                bNeedsApproval |= !ExistingResult && !bSemanticCacheHit
                    && ToolExecutor.RequiresApproval(Call);
            }
            if (Counters.ToolCalls > Budget.MaxToolCalls
                || NewToolCallCount > Budget.MaxToolCalls - Counters.ToolCalls)
            {
                return Finish(EAgentStatus::Failed,
                    "Agent tool-call budget exhausted before approval");
            }
            if (Counters.ReadOnlyToolCalls > Budget.MaxReadOnlyToolCalls
                || NewReadOnlyCallCount
                    > Budget.MaxReadOnlyToolCalls - Counters.ReadOnlyToolCalls)
            {
                return Finish(EAgentStatus::Failed,
                    "Agent read-only tool budget exhausted before approval");
            }
            if (Counters.MutationToolCalls > Budget.MaxMutationToolCalls
                || NewMutationCallCount
                    > Budget.MaxMutationToolCalls - Counters.MutationToolCalls)
            {
                return Finish(EAgentStatus::Failed,
                    "Agent mutation tool budget exhausted before approval");
            }
            if (bNeedsApproval)
            {
                if (!Transition(EAgentStatus::AwaitingApproval, Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error));
                }
                for (const FAgentToolCall& Call : Response.ToolCalls)
                {
                    if (!Session.FindToolResult(Call.Id)
                        && ToolExecutor.RequiresApproval(Call))
                    {
                        ToolExecutor.PrepareApproval(Call);
                    }
                }
            }
            if (!Transition(EAgentStatus::ExecutingTool, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
            bool bToolFailed = false;
            bool bMadeProgress = false;
            for (const FAgentToolCall& Call : Response.ToolCalls)
            {
                if (IsCancelled(CancellationToken))
                {
                    return Finish(EAgentStatus::Cancelled, "Agent run was cancelled");
                }
                if (Call.Id.empty() || Call.Name.empty())
                {
                    bToolFailed = true;
                    Error = "Provider emitted a tool call without a stable id or name";
                    break;
                }

                std::optional<FAgentToolResult> Existing = Session.FindToolResult(Call.Id);
                FAgentToolResult Result;
                const bool bReadOnly = ToolExecutor.IsReadOnly(Call);
                const std::string SemanticKey = bReadOnly
                    ? MakeSemanticKey(Call) : std::string {};
                bool bSemanticCacheHit = false;
                if (Existing)
                {
                    Result = *Existing;
                    Result.bReused = true;
                }
                else
                {
                    FAgentEvent CallEvent;
                    CallEvent.Type = EAgentEventType::ToolCall;
                    CallEvent.CallId = Call.Id;
                    CallEvent.ToolName = Call.Name;
                    CallEvent.PayloadJson = Call.ArgumentsJson;
                    if (!Session.Append(std::move(CallEvent), &Error))
                    {
                        return Finish(EAgentStatus::Failed, std::move(Error));
                    }
                    const auto Cached = bReadOnly
                        ? ReadOnlyCache.find(SemanticKey) : ReadOnlyCache.end();
                    if (Cached != ReadOnlyCache.end())
                    {
                        Result = MakeSemanticCacheResult(Call, Cached->second);
                        bSemanticCacheHit = true;
                        ++Counters.SemanticCacheHits;
                    }
                    else
                    {
                        if (Counters.ToolCalls >= Budget.MaxToolCalls)
                        {
                            return Finish(EAgentStatus::Failed,
                                "Agent tool-call budget exhausted");
                        }
                        ++Counters.ToolCalls;
                        if (bReadOnly) ++Counters.ReadOnlyToolCalls;
                        else ++Counters.MutationToolCalls;
                        Result = ToolExecutor.Execute(Call, CancellationToken);
                        Result.CallId = Call.Id;
                        if (Result.bSucceeded)
                        {
                            bMadeProgress = true;
                            if (bReadOnly)
                                ReadOnlyCache[SemanticKey] = Result;
                            else
                                ++StateRevision;
                        }
                    }
                }

                if (Result.bSucceeded)
                {
                    ProgressActions.push_back(
                        {Call.Name, bReadOnly, Existing.has_value() || bSemanticCacheHit,
                            StateRevision});
                    if (ProgressActions.size() > 12)
                        ProgressActions.erase(ProgressActions.begin());
                }

                FAgentEvent ResultEvent;
                ResultEvent.Type = EAgentEventType::ToolResult;
                ResultEvent.Role = EAgentRole::Tool;
                ResultEvent.CallId = Call.Id;
                ResultEvent.ToolName = Call.Name;
                ResultEvent.PayloadJson = Result.OutputJson;
                ResultEvent.TraceJson = bSemanticCacheHit
                    ? R"([{"stage":"Execute","succeeded":true,"message":"Semantic read cache hit; tool handler was not called"}])"
                    : ToolExecutor.GetLastExecutionTraceJson();
                ResultEvent.Content = Result.Error;
                ResultEvent.bSucceeded = Result.bSucceeded;
                ResultEvent.bReused = Result.bReused;
                if (!Session.Append(std::move(ResultEvent), &Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error));
                }
                if (!Result.bSucceeded)
                {
                    bToolFailed = true;
                    Error = Result.Error.empty() ? "Tool execution failed" : Result.Error;
                    break;
                }
            }

            if (bToolFailed)
            {
                if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
                {
                    return Finish(EAgentStatus::Failed, Error);
                }
                ++Counters.RepairAttempts;
                if (!Transition(EAgentStatus::Repairing, Error)
                    || !Session.WriteCheckpoint(EAgentStatus::Repairing, Counters, &Error)
                    || !Transition(EAgentStatus::Planning, Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error));
                }
                continue;
            }

            if (bMadeProgress)
            {
                Counters.ConsecutiveNoProgressSteps = 0;
            }
            else
            {
                ++Counters.ConsecutiveNoProgressSteps;
                if (Budget.MaxConsecutiveNoProgressSteps > 0
                    && Counters.ConsecutiveNoProgressSteps
                    >= Budget.MaxConsecutiveNoProgressSteps)
                {
                    return Finish(EAgentStatus::Failed,
                        "Agent stopped after repeated tool calls made no progress; "
                        "use the cached facts, perform a state-changing action, ask the "
                        "user for missing information, or return a final answer");
                }
            }

            if (!Transition(EAgentStatus::Validating, Error)
                || !Session.WriteCheckpoint(EAgentStatus::Validating, Counters, &Error)
                || !Transition(EAgentStatus::Planning, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
            continue;
        }

        if (Response.bFinal)
        {
            FAgentRunResult Result = Finish(EAgentStatus::Completed);
            Result.FinalText = Response.Content;
            return Result;
        }

        if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
        {
            return Finish(EAgentStatus::Failed, "Provider returned neither a final answer nor a tool call");
        }
        ++Counters.RepairAttempts;
        if (!Transition(EAgentStatus::Repairing, Error)
            || !Session.WriteCheckpoint(EAgentStatus::Repairing, Counters, &Error)
            || !Transition(EAgentStatus::Planning, Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error));
        }
    }
}

std::string FAgentRuntime::MakeSemanticKey(const FAgentToolCall& Call) const
{
    std::string CanonicalArguments = Call.ArgumentsJson;
    try
    {
        CanonicalArguments = FJson::parse(Call.ArgumentsJson).dump();
    }
    catch (...)
    {
        // Schema validation still owns malformed input diagnostics.
    }
    return std::to_string(StateRevision) + "\n" + Call.Name + "\n"
        + CanonicalArguments;
}

std::string FAgentRuntime::BuildProgressLedgerJson() const
{
    FJson Actions = FJson::array();
    for (const FProgressAction& Action : ProgressActions)
    {
        Actions.push_back({{"tool", Action.ToolName},
            {"kind", Action.bReadOnly ? "read" : "mutation"},
            {"reused", Action.bReused},
            {"state_revision", Action.StateRevision}});
    }
    return FJson {{"goal", CurrentGoal},
        {"state_revision", StateRevision},
        {"completed_actions", std::move(Actions)},
        {"budget", {{"steps_used", Counters.Steps},
            {"steps_remaining", Counters.Steps < Budget.MaxSteps
                ? Budget.MaxSteps - Counters.Steps : 0},
            {"read_calls_used", Counters.ReadOnlyToolCalls},
            {"read_calls_remaining", Counters.ReadOnlyToolCalls
                    < Budget.MaxReadOnlyToolCalls
                ? Budget.MaxReadOnlyToolCalls - Counters.ReadOnlyToolCalls : 0},
            {"mutation_calls_used", Counters.MutationToolCalls},
            {"mutation_calls_remaining", Counters.MutationToolCalls
                    < Budget.MaxMutationToolCalls
                ? Budget.MaxMutationToolCalls - Counters.MutationToolCalls : 0},
            {"semantic_cache_hits", Counters.SemanticCacheHits},
            {"consecutive_no_progress_steps",
                Counters.ConsecutiveNoProgressSteps}}},
        {"next_action_rule",
            "Do not repeat a completed read at the same state revision. Mutate once "
            "arguments are known, ask for missing information, or finish."}}.dump();
}

FAgentToolResult FAgentRuntime::MakeSemanticCacheResult(
    const FAgentToolCall& Call,
    const FAgentToolResult& Cached) const
{
    FAgentToolResult Result = Cached;
    Result.CallId = Call.Id;
    Result.bReused = true;
    try
    {
        FJson Output = FJson::parse(Result.OutputJson);
        if (Output.is_object())
        {
            Output["_pico_harness"] = {{"semantic_cache_hit", true},
                {"state_revision", StateRevision},
                {"guidance", "Use this existing result; do not issue the same read again."}};
        }
        else
        {
            Output = {{"cached_result", std::move(Output)},
                {"_pico_harness", {{"semantic_cache_hit", true},
                    {"state_revision", StateRevision},
                    {"guidance", "Use this existing result; do not issue the same read again."}}}};
        }
        Result.OutputJson = Output.dump();
    }
    catch (...)
    {
        Result.OutputJson = FJson {{"cached_result", Result.OutputJson},
            {"_pico_harness", {{"semantic_cache_hit", true},
                {"state_revision", StateRevision}}}}.dump();
    }
    return Result;
}

bool FAgentRuntime::IsCancelled(const FCancellationToken* CancellationToken) const
{
    return CancellationToken && CancellationToken->IsCancellationRequested();
}

bool FAgentRuntime::CheckBudget(std::string& OutError) const
{
    if (Counters.Steps >= Budget.MaxSteps)
    {
        OutError = "Agent step budget exhausted";
        return false;
    }
    const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - StartTime).count();
    if (Elapsed >= static_cast<std::int64_t>(Budget.MaxElapsedMilliseconds))
    {
        OutError = "Agent elapsed-time budget exhausted";
        return false;
    }
    return true;
}

bool FAgentRuntime::Transition(EAgentStatus Status, std::string& OutError)
{
    if (!IsAllowedAgentTransition(Session.GetStatus(), Status))
    {
        OutError = "Invalid Agent state transition from "
            + std::string(ToString(Session.GetStatus())) + " to "
            + std::string(ToString(Status));
        return false;
    }
    return Session.SetStatus(Status, &OutError);
}

FAgentRunResult FAgentRuntime::Finish(EAgentStatus Status, std::string Error)
{
    std::string PersistenceError;
    if (!Error.empty())
    {
        FAgentEvent Event;
        Event.Type = EAgentEventType::Error;
        Event.Content = Error;
        Session.Append(std::move(Event), &PersistenceError);
    }
    Session.SetStatus(Status, &PersistenceError);
    Session.WriteCheckpoint(Status, Counters, &PersistenceError);
    if (!PersistenceError.empty() && Error.empty()) Error = PersistenceError;
    return {Status, {}, std::move(Error), Counters};
}
}
