#include "Pico/Agent/AgentRuntime.h"

#include "Pico/Agent/AgentMetrics.h"

#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::string MakeTraceId(std::string_view Prefix)
{
    static std::atomic<std::uint64_t> Counter {0};
    const auto Now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::string(Prefix) + "_" + std::to_string(Now) + "_"
        + std::to_string(Counter.fetch_add(1, std::memory_order_relaxed));
}

std::int64_t NowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::uint64_t MeasureRequestContextBytes(const FAgentProviderRequest& Request)
{
    std::uint64_t Bytes = Request.TaskStateJson.size()
        + Request.ProgressLedgerJson.size()
        + Request.KnowledgeContextJson.size() + Request.SkillContextJson.size();
    for (const FAgentMessage& Message : Request.Messages)
    {
        Bytes += Message.Content.size() + Message.ToolCallId.size();
        for (const FAgentToolCall& Call : Message.ToolCalls)
            Bytes += Call.Id.size() + Call.Name.size() + Call.ArgumentsJson.size();
    }
    return Bytes;
}

void HashAppend(std::uint64_t& Hash, std::string_view Value)
{
    for (const unsigned char Byte : Value)
    {
        Hash ^= Byte;
        Hash *= 1099511628211ULL;
    }
}

std::string Fingerprint(std::string_view Value)
{
    std::uint64_t Hash = 1469598103934665603ULL;
    HashAppend(Hash, Value);
    return std::to_string(Hash);
}

FJson BuildRequestProfile(const FAgentProviderRequest& Request,
    std::uint64_t HistoryReplayMicroseconds,
    std::uint64_t KnowledgeRefreshMicroseconds)
{
    std::uint64_t HistoryBytes = 0;
    std::uint64_t ToolResultBytes = 0;
    std::uint64_t HistoryHash = 1469598103934665603ULL;
    std::uint64_t ToolResultHash = 1469598103934665603ULL;
    for (const FAgentMessage& Message : Request.Messages)
    {
        const std::uint64_t MessageBytes = Message.Content.size()
            + Message.ToolCallId.size();
        HistoryBytes += MessageBytes;
        HashAppend(HistoryHash, std::to_string(static_cast<int>(Message.Role)));
        HashAppend(HistoryHash, std::to_string(Message.Content.size()));
        HashAppend(HistoryHash, Message.Content);
        HashAppend(HistoryHash, Message.ToolCallId);
        if (Message.Role == EAgentRole::Tool)
        {
            ToolResultBytes += MessageBytes;
            HashAppend(ToolResultHash, Message.ToolCallId);
            HashAppend(ToolResultHash, Message.Content);
        }
        for (const FAgentToolCall& Call : Message.ToolCalls)
        {
            const std::uint64_t CallBytes = Call.Id.size()
                + Call.Name.size() + Call.ArgumentsJson.size();
            HistoryBytes += CallBytes;
            HashAppend(HistoryHash, Call.Id);
            HashAppend(HistoryHash, Call.Name);
            HashAppend(HistoryHash, Call.ArgumentsJson);
        }
    }
    const auto Section = [](std::string_view Content)
    {
        return FJson { {"bytes", Content.size()},
            {"fingerprint", Fingerprint(Content)} };
    };
    return { {"system_prompt", FJson::object()},
        {"tool_schema", FJson::object()},
        {"skill", Section(Request.SkillContextJson)},
        {"knowledge", Section(Request.KnowledgeContextJson)},
        {"task_state", Section(Request.TaskStateJson)},
        {"progress_ledger", Section(Request.ProgressLedgerJson)},
        {"history", {{"bytes", HistoryBytes},
            {"fingerprint", std::to_string(HistoryHash)},
            {"messages", Request.Messages.size()}}},
        {"tool_results", {{"bytes", ToolResultBytes},
            {"fingerprint", std::to_string(ToolResultHash)}}},
        {"history_replay_us", HistoryReplayMicroseconds},
        {"knowledge_refresh_us", KnowledgeRefreshMicroseconds} };
}
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
    , Revisions(InSession.BuildRevisionSnapshot())
    , Counters(InSession.GetStatus() == EAgentStatus::Planning
            || InSession.GetStatus() == EAgentStatus::AwaitingApproval
            || InSession.GetStatus() == EAgentStatus::ExecutingTool
            || InSession.GetStatus() == EAgentStatus::Validating
            || InSession.GetStatus() == EAgentStatus::Repairing
        ? InSession.GetCounters()
        : FAgentCounters {})
{
    const std::string SavedTaskState = InSession.GetLatestTaskStateJson();
    if (SavedTaskState != "{}")
        DeserializeAgentTaskState(SavedTaskState, TaskState);
    for (const auto& [Domain, Revision] : Revisions)
        RevisionEpoch += Revision;
}

FAgentRunResult FAgentRuntime::Run(
    std::string Prompt,
    const FCancellationToken* CancellationToken)
{
    StartTime = std::chrono::steady_clock::now();
    ContextBytes = 0;
    ContextMetrics = {};
    RunId = MakeTraceId("run");
    TurnId.clear();
    RunSpan = BeginSpan("AgentRun", {});
    ToolExecutor.BeginRun(RunId);
    std::string Error;
    ToolReplaySequenceFloor = 0;
    bReflectionUsed = false;
    ReflectionJson = "{}";
    if (!Prompt.empty())
    {
        const std::vector<FAgentEvent>& ExistingEvents = Session.GetEvents();
        ToolReplaySequenceFloor = ExistingEvents.empty()
            ? 0 : ExistingEvents.back().Sequence + 1;
        ProgressActions.clear();
        RecentObservations.clear();
        RecentActions.clear();
        bObservedToolActivity = false;
        TaskState = {};
        TaskState.Goal = Prompt;
        TaskState.SuccessCriteria = {
            "At least one successful verified tool observation exists"};
        TaskState.CriterionEvidence = {{TaskState.SuccessCriteria.front(), {}, false}};
        TaskState.Revision = 1;
        bReflectionUsed = false;
        FAgentEvent UserMessage;
        UserMessage.Type = EAgentEventType::Message;
        UserMessage.Role = EAgentRole::User;
        UserMessage.Content = std::move(Prompt);
        if (!Session.Append(std::move(UserMessage), &Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error),
                EAgentFailureClass::Infrastructure);
        }
    }
    else if (TaskState.Goal.empty())
    {
        const std::vector<FAgentMessage> History = Session.BuildMessageHistory();
        for (auto It = History.rbegin(); It != History.rend(); ++It)
        {
            if (It->Role == EAgentRole::User)
            {
                TaskState.Goal = It->Content;
                TaskState.Revision = 1;
                break;
            }
        }
    }
    if (Counters.ReflectionAttempts > 0
        && ReflectionJson == "{}"
        && TaskState.CurrentStep == "Conditional reflection and recovery")
    {
        bReflectionUsed = true;
        ReflectionJson = FJson {
            {"active", true},
            {"attempt", Counters.ReflectionAttempts},
            {"trigger", "checkpoint_resume"},
            {"failure_class", "None"},
            {"diagnosis", "Resume the single reflection attempt persisted by the latest checkpoint."},
            {"instructions", FJson::array({
                "Use the existing verified observations before requesting another read.",
                "Choose one different next action; do not restart the failed path."
            })}
        }.dump();
    }

    if (!Transition(EAgentStatus::Planning, Error))
    {
        return Finish(EAgentStatus::Failed, std::move(Error),
            EAgentFailureClass::Infrastructure);
    }

    bool bFirstModelRequest = true;
    while (true)
    {
        if (IsCancelled(CancellationToken))
        {
            return Finish(EAgentStatus::Cancelled, "Agent run was cancelled",
                EAgentFailureClass::Cancelled);
        }
        if (!CheckBudget(Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error),
                EAgentFailureClass::BudgetExceeded);
        }

        ++Counters.Steps;
        BeginTurn();
        FAgentProviderRequest Request;
        std::uint64_t HistoryReplayMicroseconds = 0;
        if (Context.Features.bContextAssembler)
        {
            FAgentContextAssemblyInput AssemblyInput;
            const auto HistoryStarted = std::chrono::steady_clock::now();
            AssemblyInput.Messages = Session.BuildMessageHistory();
            HistoryReplayMicroseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - HistoryStarted).count());
            AssemblyInput.TaskStateJson = BuildTaskStateJson();
            AssemblyInput.ObservationContextJson = BuildProgressLedgerJson();
            AssemblyInput.KnowledgeContextJson = Context.KnowledgeContextJson;
            AssemblyInput.SkillContextJson = Context.SkillContextJson;
            AssemblyInput.MaxMessages = Budget.MaxContextMessages;
            AssemblyInput.MaxBytes = Budget.MaxContextBytesPerRequest;
            AssemblyInput.bTaskBoundaryProjection =
                Context.Features.bTaskBoundaryProjection;
            FAgentAssembledContext Assembled =
                FAgentContextAssembler::Assemble(std::move(AssemblyInput));
            Request.Messages = std::move(Assembled.Messages);
            Request.TaskStateJson = std::move(Assembled.TaskStateJson);
            Request.ProgressLedgerJson =
                std::move(Assembled.ObservationContextJson);
            Request.KnowledgeContextJson =
                std::move(Assembled.KnowledgeContextJson);
            Request.SkillContextJson = std::move(Assembled.SkillContextJson);
            Counters.ContextMessages = Request.Messages.size();
            Counters.TrimmedContextMessages = Assembled.TrimmedMessages;
            ContextBytes += Assembled.Metrics.TotalBytes;
            if (Context.Features.bContextMetrics)
                AccumulateContextMetrics(Assembled.Metrics);
        }
        else
        {
            std::size_t TrimmedMessages = 0;
            const auto HistoryStarted = std::chrono::steady_clock::now();
            Request.Messages = Session.BuildBoundedMessageHistory(
                Budget.MaxContextMessages,
                Budget.MaxContextBytesPerRequest,
                &TrimmedMessages);
            HistoryReplayMicroseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - HistoryStarted).count());
            Counters.ContextMessages = Request.Messages.size();
            Counters.TrimmedContextMessages = TrimmedMessages;
            Request.TaskStateJson = BuildTaskStateJson();
            Request.ProgressLedgerJson = BuildProgressLedgerJson();
            Request.KnowledgeContextJson = Context.KnowledgeContextJson;
            Request.SkillContextJson = Context.SkillContextJson;
            ContextBytes += MeasureRequestContextBytes(Request);
        }
        Request.OnTextDelta = Context.OnAssistantDelta;
        Request.Step = Counters.Steps;
        Request.RepairAttempt = Counters.RepairAttempts;
        FActiveSpan ModelSpan = BeginSpan("Model.Generate", TurnSpan.Id);
        FJson RequestProfile = BuildRequestProfile(Request,
            HistoryReplayMicroseconds,
            bFirstModelRequest ? Context.KnowledgeRefreshMicroseconds : 0);
        bFirstModelRequest = false;
        FAgentProviderResponse Response;
        const bool bInjectedProviderTimeout = ConsumeFailureInjection(
            EAgentFailureInjectionPoint::ProviderTimeout);
        if (bInjectedProviderTimeout)
        {
            Response.bSucceeded = false;
            Response.Error = "Injected provider timeout";
            Response.FailureClass = EAgentFailureClass::Infrastructure;
        }
        else
        {
            Response = Provider.Generate(Request, CancellationToken);
        }
        if (Response.Usage.bAvailable)
        {
            ++Counters.ProviderUsageResponses;
            Counters.ProviderPromptTokens += Response.Usage.PromptTokens;
            Counters.ProviderCompletionTokens += Response.Usage.CompletionTokens;
            if (Response.Usage.bCacheDetailsAvailable)
            {
                ++Counters.ProviderCacheDetailResponses;
                Counters.ProviderCacheHitTokens += Response.Usage.CacheHitTokens;
                Counters.ProviderCacheMissTokens += Response.Usage.CacheMissTokens;
            }
        }
        if (!bInjectedProviderTimeout && ConsumeFailureInjection(
                EAgentFailureInjectionPoint::ProviderInvalidJson))
        {
            Response = {};
            Response.bSucceeded = false;
            Response.Error = "Injected invalid provider JSON";
            Response.FailureClass = EAgentFailureClass::ModelProtocol;
        }
        const bool bProviderCancelled = IsCancelled(CancellationToken)
            || Response.Error == "Cancelled";
        const FAgentProviderRequestDiagnostics& Diagnostics =
            Response.RequestDiagnostics;
        RequestProfile["system_prompt"] = {
            {"bytes", Diagnostics.SystemPromptBytes},
            {"fingerprint", Diagnostics.SystemPromptFingerprint}};
        RequestProfile["tool_schema"] = {
            {"bytes", Diagnostics.ToolSchemaBytes},
            {"fingerprint", Diagnostics.ToolSchemaFingerprint}};
        RequestProfile["task_boundary_projection_enabled"] =
            Context.Features.bContextAssembler
                && Context.Features.bTaskBoundaryProjection;
        RequestProfile["serialized_bytes"] = Diagnostics.SerializedBytes;
        RequestProfile["serialization_us"] =
            Diagnostics.SerializationMicroseconds;
        FJson ModelPayload = {{"request_profile", std::move(RequestProfile)}};
        if (Response.Usage.bAvailable)
            ModelPayload["provider_usage"] = {
                {"prompt_tokens", Response.Usage.PromptTokens},
                {"completion_tokens", Response.Usage.CompletionTokens},
                {"cache_details_available", Response.Usage.bCacheDetailsAvailable},
                {"cache_hit_tokens", Response.Usage.CacheHitTokens},
                {"cache_miss_tokens", Response.Usage.CacheMissTokens}};
        ModelSpan.PayloadJson = ModelPayload.dump();
        EndSpan(ModelSpan, Response.bSucceeded && !bProviderCancelled,
            bProviderCancelled ? "Agent run was cancelled" : Response.Error);

        if (bProviderCancelled)
        {
            return Finish(EAgentStatus::Cancelled, "Agent run was cancelled",
                EAgentFailureClass::Cancelled);
        }
        if (!Response.bSucceeded)
        {
            const EAgentFailureClass ProviderFailure =
                Response.FailureClass == EAgentFailureClass::None
                ? EAgentFailureClass::Infrastructure : Response.FailureClass;
            const FAgentRecoveryPolicy Recovery =
                GetAgentRecoveryPolicy(ProviderFailure);
            FAgentEvent Failure;
            Failure.Type = EAgentEventType::Error;
            Failure.Content = Response.Error.empty() ? "Provider failed" : Response.Error;
            Failure.FailureClass = ProviderFailure;
            Failure.RecoveryAction = Recovery.Action;
            Session.Append(std::move(Failure));
            if (!Recovery.bAutomaticallyRetryable)
            {
                return Finish(EAgentStatus::Failed,
                    Response.Error.empty() ? "Provider failed" : Response.Error,
                    ProviderFailure);
            }
            if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
            {
                return Finish(EAgentStatus::Failed, "Agent repair budget exhausted",
                    EAgentFailureClass::BudgetExceeded);
            }
            ++Counters.RepairAttempts;
            if (!Transition(EAgentStatus::Repairing, Error)
                || !WriteCheckpoint(EAgentStatus::Repairing, Error)
                || !Transition(EAgentStatus::Planning, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error),
                    EAgentFailureClass::Infrastructure);
            }
            EndTurn(false, Response.Error.empty() ? "Provider failed" : Response.Error);
            continue;
        }

        if (Response.bFinal && Context.Features.bMutationReadbackGate
            && TaskState.bMutationReadbackPending)
        {
            if (TryEnterReflection(
                    "mutation_readback_required",
                    "The proposed final answer follows a project or World mutation "
                    "that has not been independently inspected. Run a fresh read-only "
                    "describe or validation tool against the changed target before "
                    "claiming completion.",
                    EAgentFailureClass::VerificationFailed,
                    Error))
            {
                EndTurn(false,
                    "Conditional reflection: mutation requires independent readback");
                continue;
            }
            return Finish(EAgentStatus::Failed,
                "Agent final answer was rejected because the latest mutation from "
                + TaskState.PendingMutationTool
                + " was not followed by a fresh read-only inspection",
                EAgentFailureClass::VerificationFailed);
        }

        if (Response.bFinal && !HasRequiredCompletionEvidence())
        {
            if (TryEnterReflection(
                    "missing_completion_evidence",
                    "The proposed final answer has no successful verified tool "
                    "observation. Inspect the relevant target or report the limitation.",
                    EAgentFailureClass::VerificationFailed,
                    Error))
            {
                EndTurn(false, "Conditional reflection: missing completion evidence");
                continue;
            }
            return Finish(EAgentStatus::Failed,
                "Agent final answer was rejected because tool activity has no "
                "verified evidence from a successful observation",
                EAgentFailureClass::VerificationFailed);
        }

        if (!Response.Content.empty())
        {
            FAgentEvent AssistantMessage;
            AssistantMessage.Type = EAgentEventType::Message;
            AssistantMessage.Role = EAgentRole::Assistant;
            AssistantMessage.Content = Response.Content;
            if (!Session.Append(std::move(AssistantMessage), &Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error),
                    EAgentFailureClass::Infrastructure);
            }
        }

        if (!Response.ToolCalls.empty())
        {
            if (Budget.ReservedFinalSteps > 0
                && Counters.Steps > Budget.MaxSteps - std::min(
                    Budget.MaxSteps, Budget.ReservedFinalSteps))
            {
                return Finish(EAgentStatus::Failed,
                    "Agent used the step reserved for its final answer to request another tool",
                    EAgentFailureClass::BudgetExceeded);
            }
            bool bNeedsApproval = false;
            std::size_t NewToolCallCount = 0;
            std::size_t NewReadOnlyCallCount = 0;
            std::size_t NewMutationCallCount = 0;
            for (const FAgentToolCall& Call : Response.ToolCalls)
            {
                const std::optional<FAgentToolResult> ExistingResult =
                    Session.FindToolResult(Call.Id, ToolReplaySequenceFloor);
                if (ExistingResult
                    && !Session.MatchesToolCall(Call, ToolReplaySequenceFloor))
                {
                    return Finish(EAgentStatus::Failed,
                        "ToolCall id was reused with different tool or arguments: " + Call.Id,
                        EAgentFailureClass::Conflict);
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
                    "Agent tool-call budget exhausted before approval",
                    EAgentFailureClass::BudgetExceeded);
            }
            if (Counters.ReadOnlyToolCalls > Budget.MaxReadOnlyToolCalls
                || NewReadOnlyCallCount
                    > Budget.MaxReadOnlyToolCalls - Counters.ReadOnlyToolCalls)
            {
                return Finish(EAgentStatus::Failed,
                    "Agent read-only tool budget exhausted before approval",
                    EAgentFailureClass::BudgetExceeded);
            }
            if (Counters.MutationToolCalls > Budget.MaxMutationToolCalls
                || NewMutationCallCount
                    > Budget.MaxMutationToolCalls - Counters.MutationToolCalls)
            {
                return Finish(EAgentStatus::Failed,
                    "Agent mutation tool budget exhausted before approval",
                    EAgentFailureClass::BudgetExceeded);
            }
            if (bNeedsApproval)
            {
                FActiveSpan ApprovalSpan = BeginSpan("Tool.Approval", TurnSpan.Id);
                if (!Transition(EAgentStatus::AwaitingApproval, Error))
                {
                    EndSpan(ApprovalSpan, false, Error);
                    return Finish(EAgentStatus::Failed, std::move(Error),
                        EAgentFailureClass::Infrastructure);
                }
                for (const FAgentToolCall& Call : Response.ToolCalls)
                {
                    if (!Session.FindToolResult(Call.Id, ToolReplaySequenceFloor)
                        && ToolExecutor.RequiresApproval(Call))
                    {
                        ToolExecutor.PrepareApproval(Call);
                    }
                }
                EndSpan(ApprovalSpan, true);
            }
            if (!Transition(EAgentStatus::ExecutingTool, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error),
                    EAgentFailureClass::Infrastructure);
            }
            bool bToolFailed = false;
            bool bMadeProgress = false;
            bool bOscillationDetected = false;
            EAgentFailureClass ToolFailureClass = EAgentFailureClass::None;
            for (const FAgentToolCall& Call : Response.ToolCalls)
            {
                if (IsCancelled(CancellationToken))
                {
                    return Finish(EAgentStatus::Cancelled, "Agent run was cancelled",
                        EAgentFailureClass::Cancelled);
                }
                if (Call.Id.empty() || Call.Name.empty())
                {
                    bToolFailed = true;
                    ToolFailureClass = EAgentFailureClass::ModelProtocol;
                    Error = "Provider emitted a tool call without a stable id or name";
                    break;
                }

                FActiveSpan ToolSpan = BeginSpan(
                    "Tool." + Call.Name, TurnSpan.Id);

                std::optional<FAgentToolResult> Existing = Session.FindToolResult(
                    Call.Id, ToolReplaySequenceFloor);
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
                        EndSpan(ToolSpan, false, Error);
                        return Finish(EAgentStatus::Failed, std::move(Error),
                            EAgentFailureClass::Infrastructure);
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
                            EndSpan(ToolSpan, false,
                                "Agent tool-call budget exhausted");
                            return Finish(EAgentStatus::Failed,
                                "Agent tool-call budget exhausted",
                                EAgentFailureClass::BudgetExceeded);
                        }
                        ++Counters.ToolCalls;
                        if (bReadOnly) ++Counters.ReadOnlyToolCalls;
                        else ++Counters.MutationToolCalls;
                        if (ConsumeFailureInjection(
                                EAgentFailureInjectionPoint::CrashBeforeExecute))
                        {
                            EndSpan(ToolSpan, false,
                                "Injected crash before tool execution");
                            return Finish(EAgentStatus::Failed,
                                "Injected crash before tool execution",
                                EAgentFailureClass::Infrastructure);
                        }
                        Result = ToolExecutor.Execute(Call, CancellationToken);
                        if (ConsumeFailureInjection(
                                EAgentFailureInjectionPoint::CrashAfterSideEffect))
                        {
                            EndSpan(ToolSpan, false,
                                "Injected crash after tool side effect");
                            return Finish(EAgentStatus::Failed,
                                "Injected crash after tool side effect",
                                EAgentFailureClass::Infrastructure);
                        }
                        Result.CallId = Call.Id;
                        NormalizeAgentToolResult(Result);
                        if (Result.bSucceeded)
                        {
                            if (bReadOnly)
                                ReadOnlyCache[SemanticKey] = Result;
                            else
                            {
                                std::vector<std::string> WriteSet =
                                    ToolExecutor.GetRevisionWriteSet(Call);
                                if (Result.RevisionChanges.empty())
                                    for (const std::string& Domain : WriteSet)
                                        Result.RevisionChanges.push_back({Domain, 0, 0});
                                for (FAgentRevisionChange& Change : Result.RevisionChanges)
                                {
                                    const std::string Domain = Change.Domain.empty()
                                        ? "State.Revision" : Change.Domain;
                                    std::uint64_t& Revision = Revisions[Domain];
                                    Change.Domain = Domain;
                                    Change.Before = Revision;
                                    Change.After = ++Revision;
                                }
                                ++RevisionEpoch;
                            }
                        }
                    }
                }

                if (Result.bSucceeded)
                {
                    ProgressActions.push_back(
                        {Call.Name, bReadOnly, Existing.has_value() || bSemanticCacheHit,
                            Result.RevisionChanges});
                    if (ProgressActions.size() > 12)
                        ProgressActions.erase(ProgressActions.begin());
                }

                FAgentEvent ResultEvent;
                ResultEvent.Type = EAgentEventType::ToolResult;
                ResultEvent.Role = EAgentRole::Tool;
                ResultEvent.CallId = Call.Id;
                ResultEvent.ToolName = Call.Name;
                if (!Session.ExternalizeLargeToolResult(Result, 64 * 1024, &Error))
                {
                    EndSpan(ToolSpan, false, Error);
                    return Finish(EAgentStatus::Failed,
                        "Could not persist Tool Result artifact: " + Error,
                        EAgentFailureClass::Infrastructure);
                }
                if (ConsumeFailureInjection(
                        EAgentFailureInjectionPoint::CrashBeforePersist))
                {
                    EndSpan(ToolSpan, false,
                        "Injected crash before Tool Result persistence");
                    return Finish(EAgentStatus::Failed,
                        "Injected crash before Tool Result persistence",
                        EAgentFailureClass::Infrastructure);
                }
                ResultEvent.PayloadJson = Result.OutputJson;
                ResultEvent.StructuredResultJson = SerializeAgentToolResult(Result);
                ResultEvent.TraceJson = bSemanticCacheHit
                    ? R"([{"stage":"Execute","succeeded":true,"message":"Semantic read cache hit; tool handler was not called"}])"
                    : ToolExecutor.GetLastExecutionTraceJson();
                ResultEvent.Content = Result.Error;
                ResultEvent.bSucceeded = Result.bSucceeded;
                ResultEvent.bReused = Result.bReused;
                ResultEvent.FailureClass = Result.FailureClass;
                ResultEvent.RecoveryAction = Result.RecoveryAction;
                Error.clear();
                if (ConsumeFailureInjection(
                        EAgentFailureInjectionPoint::SessionAppendFailure))
                {
                    Error = "Injected session append failure";
                }
                else if (!Session.Append(std::move(ResultEvent), &Error))
                {
                    EndSpan(ToolSpan, false, Error);
                    return Finish(EAgentStatus::Failed, std::move(Error),
                        EAgentFailureClass::Infrastructure);
                }
                if (!Error.empty())
                {
                    EndSpan(ToolSpan, false, Error);
                    return Finish(EAgentStatus::Failed, std::move(Error),
                        EAgentFailureClass::Infrastructure);
                }
                ToolExecutor.CommitDurableResult(Call);
                if (Result.bSucceeded && Context.Features.bMutationReadbackGate)
                {
                    if (!bReadOnly)
                    {
                        TaskState.bMutationReadbackPending = true;
                        TaskState.PendingMutationTool = Call.Name;
                    }
                    else if (!Existing.has_value() && !bSemanticCacheHit
                        && TaskState.bMutationReadbackPending)
                    {
                        TaskState.bMutationReadbackPending = false;
                        TaskState.PendingMutationTool.clear();
                    }
                }
                if (Context.Features.bObservationMapping)
                {
                    const FAgentObservation Observation = BuildAgentObservation(
                        Call, Result, bReadOnly, true);
                    ++Counters.Observations;
                    bObservedToolActivity = true;
                    if (Context.Features.bTaskState)
                    {
                        ++TaskState.ObservationCount;
                        ++TaskState.Revision;
                    }
                    bMadeProgress |= Observation.bMadeProgress;
                    RecentObservations.push_back(Observation);
                    if (RecentObservations.size() > 8)
                        RecentObservations.erase(RecentObservations.begin());
                    if (Context.Features.bTaskState && Observation.bVerified)
                    {
                        const std::size_t EvidenceCount =
                            TaskState.EvidenceRefs.size();
                        BindAgentObservationEvidence(Observation, TaskState);
                        if (TaskState.EvidenceRefs.size() > EvidenceCount)
                            ++Counters.EvidenceBindings;
                    }
                    if (Context.Features.bActionOscillationGuard
                        && RecordActionAndDetectOscillation(Observation))
                    {
                        ++Counters.OscillationsDetected;
                        bOscillationDetected = true;
                    }
                }
                else
                {
                    bMadeProgress |= Result.bSucceeded
                        && !(Existing.has_value() || bSemanticCacheHit);
                }
                EndSpan(ToolSpan, Result.bSucceeded, Result.Error);
                if (!Result.bSucceeded)
                {
                    bToolFailed = true;
                    ToolFailureClass = Result.FailureClass == EAgentFailureClass::None
                        ? EAgentFailureClass::ExecutionFailed : Result.FailureClass;
                    Error = Result.Error.empty() ? "Tool execution failed" : Result.Error;
                    break;
                }
                if (bOscillationDetected) break;
            }

            if (bToolFailed)
            {
                const FAgentRecoveryPolicy Recovery =
                    GetAgentRecoveryPolicy(ToolFailureClass);
                if (!Recovery.bAutomaticallyRetryable
                    && ToolFailureClass == EAgentFailureClass::VerificationFailed
                    && TryEnterReflection(
                        "verification_failed",
                        Error.empty() ? "A tool postcondition was not verified." : Error,
                        ToolFailureClass,
                        Error))
                {
                    EndTurn(false, "Conditional reflection: verification failed");
                    continue;
                }
                if (!Recovery.bAutomaticallyRetryable)
                {
                    return Finish(EAgentStatus::Failed, Error, ToolFailureClass);
                }
                if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
                {
                    return Finish(EAgentStatus::Failed,
                        "Agent repair budget exhausted after: " + Error,
                        EAgentFailureClass::BudgetExceeded);
                }
                ++Counters.RepairAttempts;
                if (!Transition(EAgentStatus::Repairing, Error)
                    || !WriteCheckpoint(EAgentStatus::Repairing, Error)
                    || !Transition(EAgentStatus::Planning, Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error),
                        EAgentFailureClass::Infrastructure);
                }
                EndTurn(false, Error);
                continue;
            }

            if (bOscillationDetected)
            {
                if (TryEnterReflection(
                        "action_oscillation",
                        "An A-A or A-B-A action pattern repeated at an unchanged "
                        "revision without producing progress.",
                        EAgentFailureClass::BudgetExceeded,
                        Error))
                {
                    EndTurn(false, "Conditional reflection: action oscillation");
                    continue;
                }
                return Finish(EAgentStatus::Failed,
                    "Agent stopped after repeated actions made no progress: an A-A "
                    "or A-B-A oscillation was detected at an unchanged revision; "
                    "use the existing observations, perform a "
                    "state-changing action, ask for missing information, or finish",
                    EAgentFailureClass::BudgetExceeded);
            }

            if (bMadeProgress)
            {
                Counters.ConsecutiveNoProgressSteps = 0;
                if (bReflectionUsed && ReflectionJson != "{}")
                {
                    FJson Reflection = FJson::parse(ReflectionJson);
                    Reflection["active"] = false;
                    Reflection["resolved_by_progress"] = true;
                    Reflection["resolved_revision_epoch"] = RevisionEpoch;
                    ReflectionJson = Reflection.dump();
                    if (Context.Features.bTaskState)
                    {
                        TaskState.CurrentStep = "Validate recovered progress";
                        ++TaskState.Revision;
                    }
                    bReflectionUsed = false;
                }
            }
            else
            {
                ++Counters.ConsecutiveNoProgressSteps;
                if (Budget.MaxConsecutiveNoProgressSteps > 0
                    && Counters.ConsecutiveNoProgressSteps
                    >= Budget.MaxConsecutiveNoProgressSteps)
                {
                    if (TryEnterReflection(
                            "no_progress",
                            "The recent tool sequence consumed the no-progress budget "
                            "without a new verified fact or revision change.",
                            EAgentFailureClass::BudgetExceeded,
                            Error))
                    {
                        Counters.ConsecutiveNoProgressSteps = 0;
                        EndTurn(false, "Conditional reflection: no progress");
                        continue;
                    }
                    return Finish(EAgentStatus::Failed,
                        "Agent stopped after repeated tool calls made no progress; "
                        "use the cached facts, perform a state-changing action, ask the "
                        "user for missing information, or return a final answer",
                        EAgentFailureClass::BudgetExceeded);
                }
            }

            FActiveSpan ValidationSpan = BeginSpan("Run.Validation", TurnSpan.Id);
            if (!Transition(EAgentStatus::Validating, Error)
                || !WriteCheckpoint(EAgentStatus::Validating, Error)
                || !Transition(EAgentStatus::Planning, Error))
            {
                EndSpan(ValidationSpan, false, Error);
                return Finish(EAgentStatus::Failed, std::move(Error),
                    EAgentFailureClass::Infrastructure);
            }
            EndSpan(ValidationSpan, true);
            EndTurn(true);
            continue;
        }

        if (Response.bFinal)
        {
            FAgentRunResult Result = Finish(EAgentStatus::Completed);
            Result.FinalText = Response.Content;
            return Result;
        }

        if (TryEnterReflection(
                "empty_model_action",
                "The model returned neither a final answer nor a tool call.",
                EAgentFailureClass::ModelProtocol,
                Error))
        {
            EndTurn(false, "Conditional reflection: empty model action");
            continue;
        }
        if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
        {
            return Finish(EAgentStatus::Failed,
                "Provider returned neither a final answer nor a tool call",
                EAgentFailureClass::BudgetExceeded);
        }
        ++Counters.RepairAttempts;
        if (!Transition(EAgentStatus::Repairing, Error)
            || !WriteCheckpoint(EAgentStatus::Repairing, Error)
            || !Transition(EAgentStatus::Planning, Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error),
                EAgentFailureClass::Infrastructure);
        }
        EndTurn(false, "Provider returned neither a final answer nor a tool call");
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
    std::vector<std::string> ReadSet = ToolExecutor.GetRevisionReadSet(Call);
    std::sort(ReadSet.begin(), ReadSet.end());
    std::string RevisionKey;
    for (const std::string& Domain : ReadSet)
    {
        const auto Found = Revisions.find(Domain);
        RevisionKey += Domain + "=" + std::to_string(
            Found == Revisions.end() ? 0 : Found->second) + ";";
    }
    return RevisionKey + "\n" + Call.Name + "\n" + CanonicalArguments;
}

bool FAgentRuntime::RecordActionAndDetectOscillation(
    const FAgentObservation& Observation)
{
    RecentActions.push_back({Observation.ActionFingerprint,
        Observation.bMadeProgress, RevisionEpoch});
    if (RecentActions.size() > 12)
        RecentActions.erase(RecentActions.begin());
    if (Observation.bMadeProgress || RecentActions.size() < 2) return false;

    const FRecentAction& Current = RecentActions.back();
    const FRecentAction& Previous = RecentActions[RecentActions.size() - 2];
    if (!Previous.bMadeProgress && Current.Fingerprint == Previous.Fingerprint
        && Current.RevisionEpoch == Previous.RevisionEpoch)
        return true;

    if (RecentActions.size() < 3) return false;
    const FRecentAction& TwoBack = RecentActions[RecentActions.size() - 3];
    return !Previous.bMadeProgress && Current.Fingerprint == TwoBack.Fingerprint
        && Current.RevisionEpoch == Previous.RevisionEpoch
        && Current.RevisionEpoch == TwoBack.RevisionEpoch;
}

bool FAgentRuntime::HasRequiredCompletionEvidence() const
{
    if (!Context.Features.bEvidenceCompletionGate
        || !Context.Features.bTaskState
        || !Context.Features.bObservationMapping
        || (!bObservedToolActivity && TaskState.ObservationCount == 0))
        return true;
    return HasAgentCompletionEvidence(TaskState);
}

bool FAgentRuntime::TryEnterReflection(
    std::string Trigger,
    std::string Diagnosis,
    EAgentFailureClass FailureClass,
    std::string& OutError)
{
    if (!Context.Features.bConditionalReflection || bReflectionUsed
        || Counters.ReflectionAttempts >= Budget.MaxRepairAttempts)
    {
        if (bReflectionUsed
            || Counters.ReflectionAttempts >= Budget.MaxRepairAttempts)
            ++Counters.RecoveryEscalations;
        return false;
    }

    bReflectionUsed = true;
    ++Counters.ReflectionAttempts;
    ReflectionJson = FJson {
        {"active", true},
        {"attempt", Counters.ReflectionAttempts},
        {"trigger", std::move(Trigger)},
        {"failure_class", ToString(FailureClass)},
        {"diagnosis", std::move(Diagnosis)},
        {"instructions", FJson::array({
            "Use the existing verified observations before requesting another read.",
            "Identify the failed assumption and choose one different next action.",
            "Do not repeat an unchanged action fingerprint.",
            "If evidence or authorization is unavailable, ask the user or stop honestly."
        })}
    }.dump();
    if (Context.Features.bTaskState)
    {
        TaskState.CurrentStep = "Conditional reflection and recovery";
        ++TaskState.Revision;
    }

    return Transition(EAgentStatus::Repairing, OutError)
        && WriteCheckpoint(EAgentStatus::Repairing, OutError)
        && Transition(EAgentStatus::Planning, OutError);
}

std::string FAgentRuntime::BuildProgressLedgerJson() const
{
    FJson Actions = FJson::array();
    for (const FProgressAction& Action : ProgressActions)
    {
        FJson RevisionChanges = FJson::array();
        for (const FAgentRevisionChange& Change : Action.RevisionChanges)
            RevisionChanges.push_back({{"domain", Change.Domain},
                {"before", Change.Before}, {"after", Change.After}});
        Actions.push_back({{"tool", Action.ToolName},
            {"kind", Action.bReadOnly ? "read" : "mutation"},
            {"reused", Action.bReused},
            {"revision_changes", std::move(RevisionChanges)}});
    }
    FJson Observations = FJson::array();
    for (const FAgentObservation& Observation : RecentObservations)
        Observations.push_back(FJson::parse(SerializeAgentObservation(Observation)));
    FJson Criteria = FJson::array();
    for (const FAgentCriterionEvidence& Binding : TaskState.CriterionEvidence)
        Criteria.push_back({{"criterion", Binding.Criterion},
            {"satisfied", Binding.bSatisfied},
            {"evidence_refs", Binding.EvidenceRefs}});
    return FJson {{"goal", TaskState.Goal},
        {"revisions", Revisions},
        {"revision_epoch", RevisionEpoch},
        {"recent_failure", Session.GetMostRecentError()},
        {"reflection", FJson::parse(ReflectionJson)},
        {"pending_approval", Session.GetStatus() == EAgentStatus::AwaitingApproval},
        {"completed_actions", std::move(Actions)},
        {"latest_observations", std::move(Observations)},
        {"success_criteria", std::move(Criteria)},
        {"mutation_readback_pending", TaskState.bMutationReadbackPending},
        {"pending_mutation_tool", TaskState.PendingMutationTool},
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
            {"observations", Counters.Observations},
            {"evidence_bindings", Counters.EvidenceBindings},
            {"oscillations_detected", Counters.OscillationsDetected},
            {"reflection_attempts", Counters.ReflectionAttempts},
            {"recovery_escalations", Counters.RecoveryEscalations},
            {"consecutive_no_progress_steps",
                Counters.ConsecutiveNoProgressSteps},
            {"context_messages", Counters.ContextMessages},
            {"trimmed_context_messages", Counters.TrimmedContextMessages}}},
        {"next_action_rule",
            "Do not repeat a completed read at the same relevant revision. Mutate once "
            "arguments are known. After a mutation, inspect the exact changed target "
            "with a fresh read-only tool before finishing. Skills are recommendations, "
            "not capability restrictions; compose other available tools when needed. "
            "If a required capability is truly absent, state the exact gap and provide "
            "concrete executable alternatives instead of a generic refusal."}}.dump();
}

std::string FAgentRuntime::BuildTaskStateJson() const
{
    if (!Context.Features.bTaskState) return "{}";
    if (Context.CurrentEditorStateJson.empty()
        || Context.CurrentEditorStateJson == "{}")
        return SerializeAgentTaskState(TaskState);
    FJson State = FJson::parse(SerializeAgentTaskState(TaskState));
    State["current_editor_state"] = FJson::parse(
        Context.CurrentEditorStateJson);
    return State.dump();
}

bool FAgentRuntime::WriteCheckpoint(
    EAgentStatus Status,
    std::string& OutError)
{
    return Session.WriteCheckpoint(
        Status, Counters, &OutError, BuildTaskStateJson());
}

void FAgentRuntime::AccumulateContextMetrics(
    const FAgentContextMetrics& Metrics)
{
    ContextMetrics.AssemblyCount += Metrics.AssemblyCount;
    ContextMetrics.TotalAssemblyMicroseconds +=
        Metrics.TotalAssemblyMicroseconds;
    ContextMetrics.MaxAssemblyMicroseconds = std::max(
        ContextMetrics.MaxAssemblyMicroseconds,
        Metrics.MaxAssemblyMicroseconds);
    ContextMetrics.InstructionBytes += Metrics.InstructionBytes;
    ContextMetrics.TaskStateBytes += Metrics.TaskStateBytes;
    ContextMetrics.ConversationBytes += Metrics.ConversationBytes;
    ContextMetrics.MemoryBytes += Metrics.MemoryBytes;
    ContextMetrics.ObservationBytes += Metrics.ObservationBytes;
    ContextMetrics.DroppedBytes += Metrics.DroppedBytes;
    ContextMetrics.TotalBytes += Metrics.TotalBytes;
    ContextMetrics.ProjectedHistoryBytes += Metrics.ProjectedHistoryBytes;
    ContextMetrics.ProjectedMessages += Metrics.ProjectedMessages;
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
                {"revisions", Revisions},
                {"guidance", "Use this existing result; do not issue the same read again."}};
        }
        else
        {
            Output = {{"cached_result", std::move(Output)},
                {"_pico_harness", {{"semantic_cache_hit", true},
                    {"revisions", Revisions},
                    {"guidance", "Use this existing result; do not issue the same read again."}}}};
        }
        Result.OutputJson = Output.dump();
    }
    catch (...)
    {
        Result.OutputJson = FJson {{"cached_result", Result.OutputJson},
            {"_pico_harness", {{"semantic_cache_hit", true},
            {"revisions", Revisions}}}}.dump();
    }
    Result.FactsJson = Result.OutputJson;
    NormalizeAgentToolResult(Result);
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

bool FAgentRuntime::ConsumeFailureInjection(
    EAgentFailureInjectionPoint Point)
{
    return Context.FailureInjector && Context.FailureInjector(Point);
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

FAgentRunResult FAgentRuntime::Finish(
    EAgentStatus Status,
    std::string Error,
    EAgentFailureClass FailureClass)
{
    if (Status == EAgentStatus::Completed)
        FailureClass = EAgentFailureClass::None;
    else if (Status == EAgentStatus::Cancelled)
        FailureClass = EAgentFailureClass::Cancelled;
    else if (Status == EAgentStatus::Failed
        && FailureClass == EAgentFailureClass::None)
        FailureClass = EAgentFailureClass::Infrastructure;
    const FAgentRecoveryPolicy Recovery = GetAgentRecoveryPolicy(FailureClass);

    std::string PersistenceError;
    if (!Error.empty())
    {
        FAgentEvent Event;
        Event.Type = EAgentEventType::Error;
        Event.Content = Error;
        Event.FailureClass = FailureClass;
        Event.RecoveryAction = Recovery.Action;
        Session.Append(std::move(Event), &PersistenceError);
    }
    Session.SetStatus(Status, &PersistenceError);
    Session.WriteCheckpoint(
        Status, Counters, &PersistenceError, BuildTaskStateJson());
    if (!PersistenceError.empty() && Error.empty()) Error = PersistenceError;
    ToolExecutor.EndRun(RunId, Status);
    EndTurn(Status == EAgentStatus::Completed, Error);
    EndSpan(RunSpan, Status == EAgentStatus::Completed, Error);
    Session.SetTraceContext({}, {}, {});
    FAgentRunResult Result;
    Result.Status = Status;
    Result.Error = std::move(Error);
    Result.Counters = Counters;
    Result.RunId = RunId;
    Result.ContextBytes = ContextBytes;
    Result.ContextMetrics = ContextMetrics;
    Result.bTaskBoundaryProjectionEnabled = Context.Features.bContextAssembler
        && Context.Features.bTaskBoundaryProjection;
    Result.FailureClass = FailureClass;
    Result.RecoveryAction = Recovery.Action;
    const FAgentRunMetrics Metrics = BuildAgentRunMetrics(
        Session, Result, ContextBytes);
    const std::filesystem::path MetricsPath =
        Session.GetEventLogPath().parent_path() / "Metrics" / (RunId + ".json");
    std::string MetricsError;
    if (Metrics.WriteJson(MetricsPath, &MetricsError))
        Result.MetricsPath = MetricsPath.string();
    else if (Result.Error.empty())
    {
        Result.Error = "Could not persist Agent Metrics: " + MetricsError;
        Result.FailureClass = EAgentFailureClass::Infrastructure;
        Result.RecoveryAction = GetAgentRecoveryPolicy(
            Result.FailureClass).Action;
    }
    return Result;
}

FAgentRuntime::FActiveSpan FAgentRuntime::BeginSpan(
    std::string Name,
    std::string ParentId)
{
    FActiveSpan Span;
    Span.Id = MakeTraceId("span");
    Span.ParentId = std::move(ParentId);
    Span.Name = std::move(Name);
    Span.StartedTimestampMilliseconds = NowMilliseconds();
    Span.StartedAt = std::chrono::steady_clock::now();
    Session.SetTraceContext(RunId, TurnId, Span.Id);
    return Span;
}

void FAgentRuntime::EndSpan(
    FActiveSpan& Span,
    bool bSucceeded,
    std::string Error)
{
    if (Span.Id.empty()) return;
    const auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - Span.StartedAt).count();
    FAgentEvent Event;
    Event.Type = EAgentEventType::TraceSpan;
    Event.RunId = RunId;
    Event.TurnId = TurnId;
    Event.SpanId = Span.Id;
    Event.ParentSpanId = Span.ParentId;
    Event.SpanName = Span.Name;
    Event.PayloadJson = std::move(Span.PayloadJson);
    Event.StartedTimestampMilliseconds = Span.StartedTimestampMilliseconds;
    Event.DurationMicroseconds = Duration > 0
        ? static_cast<std::uint64_t>(Duration) : 0;
    Event.bSucceeded = bSucceeded;
    Event.Content = std::move(Error);
    Session.Append(std::move(Event));
    const std::string ParentId = Span.ParentId;
    Span.Id.clear();
    Session.SetTraceContext(RunId, TurnId, ParentId);
}

void FAgentRuntime::BeginTurn()
{
    TurnId = MakeTraceId("turn");
    TurnSpan = BeginSpan("AgentTurn", RunSpan.Id);
}

void FAgentRuntime::EndTurn(bool bSucceeded, std::string Error)
{
    if (TurnSpan.Id.empty()) return;
    EndSpan(TurnSpan, bSucceeded, std::move(Error));
    TurnId.clear();
    Session.SetTraceContext(RunId, {}, RunSpan.Id);
}
}
