#include "TestRunner.h"

#include "Pico/Agent/AgentContext.h"
#include "Pico/Agent/AgentRuntime.h"
#include "Pico/Agent/AgentCredentialStore.h"
#include "Pico/Agent/AgentEvaluation.h"
#include "Pico/Agent/AgentGameAssembly.h"
#include "Pico/Agent/AgentIntent.h"
#include "Pico/Agent/AgentKnowledgeStore.h"
#include "Pico/Agent/AgentMetrics.h"
#include "Pico/Agent/AgentOperationJournal.h"
#include "Pico/Agent/AgentProjectHandoff.h"
#include "Pico/Agent/AgentSkill.h"
#include "Pico/Agent/AgentToolRegistry.h"
#include "Pico/Agent/FakeAgentProvider.h"
#include "Pico/Agent/OpenAICompatibleProvider.h"
#include "Pico/Tasks/TaskSystem.h"


#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace std::chrono_literals;

namespace
{
class FCountingToolExecutor final : public Pico::IAgentToolExecutor
{
public:
    bool RequiresApproval(const Pico::FAgentToolCall&) const override
    {
        return bRequiresApproval;
    }

    bool IsReadOnly(const Pico::FAgentToolCall& Call) const override
    {
        return bReadOnly || (!ReadOnlyToolName.empty()
            && Call.Name == ReadOnlyToolName);
    }

    std::vector<std::string> GetRevisionReadSet(
        const Pico::FAgentToolCall& Call) const override
    {
        const auto It = ReadSets.find(Call.Name);
        return It == ReadSets.end()
            ? Pico::IAgentToolExecutor::GetRevisionReadSet(Call) : It->second;
    }

    std::vector<std::string> GetRevisionWriteSet(
        const Pico::FAgentToolCall& Call) const override
    {
        const auto It = WriteSets.find(Call.Name);
        return It == WriteSets.end()
            ? Pico::IAgentToolExecutor::GetRevisionWriteSet(Call) : It->second;
    }

    void PrepareApproval(const Pico::FAgentToolCall&) override
    {
        ++PrepareApprovalCount;
    }

    std::string GetLastExecutionTraceJson() const override
    {
        return R"([{"stage":"Execute","succeeded":true,"message":"fake"}])";
    }

    Pico::FAgentToolResult Execute(
        const Pico::FAgentToolCall& Call,
        const Pico::FCancellationToken*) override
    {
        ++Count;
        if (FailureClass != Pico::EAgentFailureClass::None)
        {
            const Pico::FAgentRecoveryPolicy Recovery =
                Pico::GetAgentRecoveryPolicy(FailureClass);
            return {Call.Id, false, "{}", "classified failure", false,
                FailureClass, Recovery.Action};
        }
        return {Call.Id, true, R"({"changed":true})", {}, false};
    }

    int Count = 0;
    int PrepareApprovalCount = 0;
    bool bRequiresApproval = false;
    bool bReadOnly = false;
    std::string ReadOnlyToolName;
    std::unordered_map<std::string, std::vector<std::string>> ReadSets;
    std::unordered_map<std::string, std::vector<std::string>> WriteSets;
    Pico::EAgentFailureClass FailureClass = Pico::EAgentFailureClass::None;
};

class FDurableFailureExecutor final : public Pico::IAgentToolExecutor
{
public:
    explicit FDurableFailureExecutor(const std::filesystem::path& Root)
        : Journal(Root)
    {
    }

    bool IsReadOnly(const Pico::FAgentToolCall&) const override
    {
        return false;
    }

    Pico::FAgentToolResult Execute(
        const Pico::FAgentToolCall& Call,
        const Pico::FCancellationToken*) override
    {
        std::string Error;
        if (auto Recovered = Journal.FindApplied(Call, &Error))
        {
            ++ReconcileCount;
            return *Recovered;
        }
        if (!Error.empty() || !Journal.Prepare(Call, &Error)
            || !Journal.MarkExecuting(Call, &Error))
        {
            return {Call.Id, false, "{}", Error, false,
                Pico::EAgentFailureClass::Infrastructure,
                Pico::EAgentRecoveryAction::Retry};
        }
        ++SideEffectCount;
        Pico::FAgentToolResult Result {
            Call.Id, true, R"({"created":true})", {}, false};
        if (!Journal.MarkApplied(Call, Result, &Error))
        {
            return {Call.Id, false, "{}", Error, false,
                Pico::EAgentFailureClass::Infrastructure,
                Pico::EAgentRecoveryAction::Retry};
        }
        return Result;
    }

    void CommitDurableResult(const Pico::FAgentToolCall& Call) override
    {
        Journal.MarkCommitted(Call);
    }

    int SideEffectCount = 0;
    int ReconcileCount = 0;
    Pico::FAgentOperationJournal Journal;
};

class FRecordingProvider final : public Pico::IAgentProvider
{
public:
    Pico::FAgentProviderResponse Generate(
        const Pico::FAgentProviderRequest& Request,
        const Pico::FCancellationToken*) override
    {
        Requests.push_back(Request);
        if (NextResponse >= Responses.size())
            return {false, false, {}, "Recording provider script exhausted", {}};
        return Responses[NextResponse++];
    }

    std::vector<Pico::FAgentProviderResponse> Responses;
    std::vector<Pico::FAgentProviderRequest> Requests;
    std::size_t NextResponse = 0;
};

class FGoldenToolExecutor final : public Pico::IAgentToolExecutor
{
public:
    explicit FGoldenToolExecutor(std::string InTaskId)
        : TaskId(std::move(InTaskId))
    {
    }

    bool RequiresApproval(const Pico::FAgentToolCall& Call) const override
    {
        return !IsReadOnly(Call);
    }

    bool IsReadOnly(const Pico::FAgentToolCall& Call) const override
    {
        return Call.Name == "editor.play.validate"
            || Call.Name == "editor.agent.list_changes"
            || Call.Name == "editor.world.describe";
    }

    std::string GetLastExecutionTraceJson() const override
    {
        return R"([{"stage":"Validate","succeeded":true},{"stage":"Permission","succeeded":true},{"stage":"Execute","succeeded":true},{"stage":"Verify","succeeded":true}])";
    }

    Pico::FAgentToolResult Execute(
        const Pico::FAgentToolCall& Call,
        const Pico::FCancellationToken*) override
    {
        ++ExecutionCounts[Call.Name];
        const auto Failure = [&](Pico::EAgentFailureClass FailureClass,
            std::string Message)
        {
            const Pico::FAgentRecoveryPolicy Recovery =
                Pico::GetAgentRecoveryPolicy(FailureClass);
            return Pico::FAgentToolResult {Call.Id, false, "{}",
                std::move(Message), false, FailureClass, Recovery.Action};
        };
        if (TaskId == "approval-denial-has-no-side-effects")
            return {Call.Id, false, "{}", "User denied tool call", false};
        if (TaskId == "unknown-tool-is-rejected")
            return Failure(Pico::EAgentFailureClass::InvalidArguments,
                "Unknown tool rejected by catalog");
        if (TaskId == "skill-forbidden-tool-is-rejected")
            return Failure(Pico::EAgentFailureClass::PermissionDenied,
                "Tool is outside the selected Skill allowlist");
        if (TaskId == "failed-mutation-cannot-claim-success")
            return Failure(Pico::EAgentFailureClass::VerificationFailed,
                "Mutation postcondition failed and was rolled back");
        if (TaskId == "project-skill-cannot-write-engine")
            return Failure(Pico::EAgentFailureClass::PermissionDenied,
                "Project Skill cannot write Engine scope");
        if (Call.Name == "editor.actor.spawn") bCubeSpawned = true;
        else if (Call.Name == "editor.object.set_properties") bPropertiesChanged = true;
        else if (Call.Name == "editor.scene.create_room") bRoomCreated = true;
        else if (Call.Name == "editor.gameplay.create_third_person_character")
            bCharacterCreated = true;
        else if (Call.Name == "editor.gameplay.asc.describe") bAscDescribed = true;
        else if (Call.Name == "editor.gameplay.configure_ability_loadout")
            bAbilityLoadoutConfigured = true;
        else if (Call.Name == "editor.graph.create") bGraphCreated = true;
        else if (Call.Name == "editor.graph.describe") bGraphDescribed = true;
        else if (Call.Name == "editor.graph.add_node") ++GraphNodesAdded;
        else if (Call.Name == "editor.graph.connect_pins") bGraphConnected = true;
        else if (Call.Name == "editor.graph.set_default") bGraphDefaultSet = true;
        else if (Call.Name == "editor.graph.validate") bGraphValidated = true;
        else if (Call.Name == "editor.graph.compile") bGraphCompiled = true;
        else if (Call.Name == "editor.play.validate") bValidated = true;
        else if (Call.Name == "editor.world.save") bSaved = true;
        else if (Call.Name == "editor.play.start") bPlaying = true;
        else if (Call.Name == "editor.project.package") bPackaged = true;
        else if (Call.Name == "editor.actor.delete_many") bBatchDeleted = true;
        else if (Call.Name == "editor.agent.revert_run") bRunReverted = true;
        else if (Call.Name == "editor.world.describe") bWorldDescribed = true;
        else if (Call.Name == "editor.engine.write_config") bEngineWritten = true;
        return {Call.Id, true, R"({"verified":true})", {}, false};
    }

    std::string TaskId;
    std::unordered_map<std::string, int> ExecutionCounts;
    bool bCubeSpawned = false;
    bool bPropertiesChanged = false;
    bool bRoomCreated = false;
    bool bCharacterCreated = false;
    bool bValidated = false;
    bool bSaved = false;
    bool bPlaying = false;
    bool bPackaged = false;
    bool bBatchDeleted = false;
    bool bRunReverted = false;
    bool bAscDescribed = false;
    bool bAbilityLoadoutConfigured = false;
    bool bGraphCreated = false;
    bool bGraphDescribed = false;
    int GraphNodesAdded = 0;
    bool bGraphConnected = false;
    bool bGraphDefaultSet = false;
    bool bGraphValidated = false;
    bool bGraphCompiled = false;
    bool bWorldDescribed = false;
    bool bEngineWritten = false;
};

class FTestApproval final : public Pico::IAgentToolApproval
{
public:
    bool RequestApproval(
        const Pico::FAgentToolCall&,
        Pico::EAgentToolPermission,
        std::string_view) override
    {
        ++RequestCount;
        return bApprove;
    }

    bool bApprove = true;
    int RequestCount = 0;
};

class FTestTransaction final : public Pico::IAgentToolTransaction
{
public:
    explicit FTestTransaction(int& InValue) : Value(InValue) {}

    bool Begin(std::string_view, std::string&) override
    {
        ++BeginCount;
        Before = Value;
        bPending = true;
        return true;
    }
    bool Commit(std::string&) override
    {
        ++CommitCount;
        bPending = false;
        return true;
    }
    bool Rollback(std::string&) override
    {
        ++RollbackCount;
        if (bPending) Value = Before;
        bPending = false;
        return true;
    }

    int& Value;
    int Before = 0;
    int BeginCount = 0;
    int CommitCount = 0;
    int RollbackCount = 0;
    bool bPending = false;
};

class FScriptedHttpTransport final : public Pico::IAgentHttpTransport
{
public:
    Pico::FAgentHttpResponse PostJson(
        const Pico::FAgentHttpRequest& Request,
        const Pico::FCancellationToken*) override
    {
        Bodies.push_back(Request.Body);
        if (NextResponse >= Responses.size())
            return {false, 0, {}, 0, "Script exhausted"};
        return Responses[NextResponse++];
    }

    Pico::FAgentHttpResponse PostJsonStream(
        const Pico::FAgentHttpRequest& Request,
        const std::function<bool(std::string_view)>& OnChunk,
        const Pico::FCancellationToken*) override
    {
        Bodies.push_back(Request.Body);
        for (const std::string& Chunk : StreamChunks)
            if (!OnChunk(Chunk))
                return {false, 0, {}, 0, "Stream callback rejected chunk"};
        return StreamResponse;
    }

    std::vector<Pico::FAgentHttpResponse> Responses;
    std::vector<std::string> Bodies;
    std::vector<std::string> StreamChunks;
    Pico::FAgentHttpResponse StreamResponse {true, 200, {}, 0, {}};
    std::size_t NextResponse = 0;
};

std::filesystem::path MakeLogPath(std::string_view Name)
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / "PicoAgentTests";
    std::filesystem::create_directories(Root);
    const std::filesystem::path Path = Root / (std::string(Name) + ".jsonl");
    std::error_code Error;
    std::filesystem::remove(Path, Error);
    return Path;
}

Pico::FAgentProviderResponse Final(std::string Text)
{
    Pico::FAgentProviderResponse Response;
    Response.bFinal = true;
    Response.Content = std::move(Text);
    return Response;
}

Pico::FAgentProviderResponse ToolCalls(
    std::initializer_list<Pico::FAgentToolCall> Calls)
{
    Pico::FAgentProviderResponse Response;
    Response.ToolCalls.assign(Calls.begin(), Calls.end());
    return Response;
}

std::unique_ptr<Pico::IAgentProvider> CreateGoldenProvider(
    const Pico::FAgentGoldenTask& Task)
{
    std::vector<Pico::FFakeAgentStep> Steps;
    if (Task.Id == "spawn-and-configure-cube")
        Steps.push_back({ToolCalls({
            {"spawn-cube", "editor.actor.spawn", R"({"kind":"Cube"})"},
            {"configure-cube", "editor.object.set_properties", R"({"scale":[2,2,2]})"}}), {}});
    else if (Task.Id == "create-collision-room")
        Steps.push_back({ToolCalls({
            {"create-room", "editor.scene.create_room", R"({"width":1000})"}}), {}});
    else if (Task.Id == "create-third-person-character")
        Steps.push_back({ToolCalls({{"create-character",
            "editor.gameplay.create_third_person_character", "{}"}}), {}});
    else if (Task.Id == "configure-character-abilities")
        Steps.push_back({ToolCalls({
            {"describe-asc", "editor.gameplay.asc.describe",
                R"({"object_path":"StarterWorld.PersistentLevel.Player"})"},
            {"configure-loadout", "editor.gameplay.configure_ability_loadout",
                R"({"object_path":"StarterWorld.PersistentLevel.Player","gravity":true,"burn":true,"freeze":true})"}}), {}});
    else if (Task.Id == "create-validate-compile-graph")
        Steps.push_back({ToolCalls({
            {"create-graph", "editor.graph.create",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph"})"},
            {"describe-graph", "editor.graph.describe",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph"})"},
            {"add-delay", "editor.graph.add_node",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph","node_type":"Delay","x":300,"y":120})"},
            {"set-delay", "editor.graph.set_default",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph","pin_id":"delay-seconds","value":"1.0"})"},
            {"connect-delay", "editor.graph.connect_pins",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph","output_pin_id":"entry-then","input_pin_id":"delay-in"})"},
            {"validate-graph", "editor.graph.validate",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph"})"},
            {"compile-graph", "editor.graph.compile",
                R"({"graph_path":"/Game/Graphs/GoldenDelay.pgraph"})"}}), {}});
    else if (Task.Id == "validate-save-and-play")
        Steps.push_back({ToolCalls({
            {"validate-play", "editor.play.validate", "{}"},
            {"save-before-play", "editor.world.save", "{}"},
            {"start-play", "editor.play.start", "{}"}}), {}});
    else if (Task.Id == "validate-save-and-package")
        Steps.push_back({ToolCalls({
            {"validate-package", "editor.play.validate", "{}"},
            {"save-before-package", "editor.world.save", "{}"},
            {"package-project", "editor.project.package", R"({"name":"Golden"})"}}), {}});
    else if (Task.Id == "approval-denial-has-no-side-effects")
        Steps.push_back({ToolCalls({{"denied-mutation",
            "editor.object.set_properties", R"({"visible":false})"}}), {}});
    else if (Task.Id == "repeated-save-is-idempotent")
    {
        const Pico::FAgentProviderResponse Save = ToolCalls(
            {{"stable-save", "editor.world.save", "{}"}});
        Steps.push_back({Save, {}});
        Steps.push_back({Save, {}});
    }
    else if (Task.Id == "modify-reflected-property-and-save")
        Steps.push_back({ToolCalls({
            {"set-reflected-property", "editor.object.set_properties",
                R"({"Tint":[0.2,0.8,0.3]})"},
            {"save-reflected-property", "editor.world.save", "{}"}}), {}});
    else if (Task.Id == "batch-delete-created-cubes")
        Steps.push_back({ToolCalls({{"delete-created-cubes",
            "editor.actor.delete_many",
            R"({"object_paths":["World.CubeA","World.CubeB","World.CubeC"]})"}}), {}});
    else if (Task.Id == "revert-agent-run")
        Steps.push_back({ToolCalls({
            {"list-agent-runs", "editor.agent.list_changes", "{}"},
            {"revert-agent-run", "editor.agent.revert_run",
                R"({"run_id":"run_previous"})"}}), {}});
    else if (Task.Id == "knowledge-prompt-injection-is-data")
        Steps.push_back({ToolCalls({{"inspect-world-safely",
            "editor.world.describe", "{}"}}), {}});
    else if (Task.Id == "unknown-tool-is-rejected")
        Steps.push_back({ToolCalls({{"unknown-tool-call",
            "editor.unknown", "{}"}}), {}});
    else if (Task.Id == "skill-forbidden-tool-is-rejected")
        Steps.push_back({ToolCalls({{"forbidden-package",
            "editor.project.package", R"({"name":"Forbidden"})"}}), {}});
    else if (Task.Id == "approval-arguments-cannot-change")
    {
        Steps.push_back({ToolCalls({{"stable-approved-call",
            "editor.object.set_properties", R"({"visible":false})"}}), {}});
        Steps.push_back({ToolCalls({{"stable-approved-call",
            "editor.object.set_properties", R"({"visible":true})"}}), {}});
    }
    else if (Task.Id == "old-call-id-replay-is-idempotent")
    {
        const Pico::FAgentProviderResponse Save = ToolCalls(
            {{"old-save-call", "editor.world.save", "{}"}});
        Steps.push_back({Save, {}});
        Steps.push_back({Save, {}});
    }
    else if (Task.Id == "repeated-query-stops-at-budget")
    {
        Steps.push_back({ToolCalls({{"query-1", "editor.world.describe", "{}"}}), {}});
        Steps.push_back({ToolCalls({{"query-2", "editor.world.describe", "{}"}}), {}});
        Steps.push_back({ToolCalls({{"query-3", "editor.world.describe", "{}"}}), {}});
    }
    else if (Task.Id == "failed-mutation-cannot-claim-success")
        Steps.push_back({ToolCalls({{"failed-mutation",
            "editor.object.set_properties", R"({"Health":999})"}}), {}});
    else if (Task.Id == "play-request-does-not-package")
        Steps.push_back({ToolCalls({
            {"adversarial-validate-play", "editor.play.validate", "{}"},
            {"adversarial-save-play", "editor.world.save", "{}"},
            {"adversarial-start-play", "editor.play.start", "{}"}}), {}});
    else if (Task.Id == "project-skill-cannot-write-engine")
        Steps.push_back({ToolCalls({{"engine-scope-write",
            "editor.engine.write_config", R"({"MaxFPS":0})"}}), {}});
    else
        return nullptr;
    Steps.push_back({Final("Golden Task finished"), {}});
    return std::make_unique<Pico::FFakeAgentProvider>(std::move(Steps));
}

void TestDeterministicCompletionAndRecovery(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("completion");
    auto Session = Pico::FAgentSession::OpenOrCreate("completion", Path);
    auto FinalResponse = Final("done");
    FinalResponse.Usage = {120, 8, 90, 30, true, true};
    Pico::FFakeAgentProvider Provider({{FinalResponse, {}}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const Pico::FAgentRunResult Result = Runtime.Run("build a scene");
    Runner.Expect(
        Result.Status == Pico::EAgentStatus::Completed
            && Result.FinalText == "done" && Result.Counters.Steps == 1
            && Result.Counters.ProviderCacheHitTokens == 90
            && Result.Counters.ProviderCacheMissTokens == 30,
        "Fake provider drives a deterministic completed run");
    const auto Metrics = Pico::BuildAgentRunMetrics(*Session, Result, 0);
    Runner.Expect(Metrics.ProviderCacheHitTokens == 90
            && Metrics.ToJson().find("\"cache_miss_tokens\": 30") != std::string::npos,
        "Provider cache usage is retained in run metrics independently of tool cache hits");
    bool bFoundResponseUsage = false;
    for (const auto& Event : Session->GetEvents())
        bFoundResponseUsage |= Event.Type == Pico::EAgentEventType::TraceSpan
            && Event.SpanName == "Model.Generate"
            && Event.PayloadJson.find("\"cache_hit_tokens\":90") != std::string::npos;
    Runner.Expect(bFoundResponseUsage,
        "Each model response records its own cache usage on the correlated trace span");

    auto Restored = Pico::FAgentSession::OpenOrCreate("completion", Path);
    Runner.Expect(
        Restored && Restored->GetStatus() == Pico::EAgentStatus::Completed
            && Restored->GetCounters().Steps == 1
            && Restored->BuildMessageHistory().size() == 2,
        "Append-only events restore status, checkpoint counters, and messages");

    Pico::FFakeAgentProvider NextTurnProvider({{Final("second done"), {}}});
    Pico::FAgentRuntime NextTurnRuntime(*Restored, NextTurnProvider, Executor);
    const Pico::FAgentRunResult NextTurn = NextTurnRuntime.Run("continue chatting");
    Runner.Expect(
        NextTurn.Status == Pico::EAgentStatus::Completed
            && NextTurn.Counters.Steps == 1
            && Restored->BuildMessageHistory().size() == 4,
        "A completed chat starts a fresh per-turn budget while preserving session history");
}

void TestFailureTaxonomyAndRecoveryPolicy(FTestRunner& Runner)
{
    using Pico::EAgentFailureClass;
    using Pico::EAgentRecoveryAction;
    const std::vector<std::pair<EAgentFailureClass, EAgentRecoveryAction>> Cases = {
        {EAgentFailureClass::None, EAgentRecoveryAction::Abort},
        {EAgentFailureClass::ModelProtocol, EAgentRecoveryAction::Replan},
        {EAgentFailureClass::InvalidArguments, EAgentRecoveryAction::Replan},
        {EAgentFailureClass::PermissionDenied, EAgentRecoveryAction::AskUser},
        {EAgentFailureClass::ApprovalRejected, EAgentRecoveryAction::WaitForApproval},
        {EAgentFailureClass::PreconditionFailed, EAgentRecoveryAction::RefreshState},
        {EAgentFailureClass::ExecutionFailed, EAgentRecoveryAction::Retry},
        {EAgentFailureClass::VerificationFailed, EAgentRecoveryAction::Rollback},
        {EAgentFailureClass::Infrastructure, EAgentRecoveryAction::Retry},
        {EAgentFailureClass::BudgetExceeded, EAgentRecoveryAction::Abort},
        {EAgentFailureClass::Conflict, EAgentRecoveryAction::RefreshState},
        {EAgentFailureClass::Cancelled, EAgentRecoveryAction::Abort}};
    bool bMappingsValid = true;
    for (const auto& [FailureClass, ExpectedAction] : Cases)
    {
        EAgentFailureClass ParsedClass = EAgentFailureClass::None;
        EAgentRecoveryAction ParsedAction = EAgentRecoveryAction::Abort;
        const Pico::FAgentRecoveryPolicy Policy =
            Pico::GetAgentRecoveryPolicy(FailureClass);
        bMappingsValid &= Policy.Action == ExpectedAction
            && Pico::TryParseAgentFailureClass(Pico::ToString(FailureClass), ParsedClass)
            && ParsedClass == FailureClass
            && Pico::TryParseAgentRecoveryAction(Pico::ToString(Policy.Action), ParsedAction)
            && ParsedAction == Policy.Action;
    }
    Runner.Expect(bMappingsValid,
        "Every Agent failure class has a stable serialized recovery policy");
    Runner.Expect(
        !Pico::GetAgentRecoveryPolicy(EAgentFailureClass::InvalidArguments)
            .bAutomaticallyRetryable
        && !Pico::GetAgentRecoveryPolicy(EAgentFailureClass::PermissionDenied)
            .bAutomaticallyRetryable
        && !Pico::GetAgentRecoveryPolicy(EAgentFailureClass::ApprovalRejected)
            .bAutomaticallyRetryable,
        "Invalid arguments, permission denial, and approval rejection never auto-retry");

    const auto Path = MakeLogPath("non-retryable-failure");
    auto Session = Pico::FAgentSession::OpenOrCreate("non-retryable-failure", Path);
    Pico::FAgentProviderResponse ToolResponse;
    ToolResponse.ToolCalls.push_back({"bad-arguments", "scene.fake", "{}"});
    Pico::FFakeAgentProvider Provider({{ToolResponse, {}}, {Final("unexpected"), {}}});
    FCountingToolExecutor Executor;
    Executor.FailureClass = EAgentFailureClass::InvalidArguments;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const Pico::FAgentRunResult Result = Runtime.Run("exercise failure policy");
    Runner.Expect(Result.Status == Pico::EAgentStatus::Failed
            && Result.FailureClass == EAgentFailureClass::InvalidArguments
            && Result.RecoveryAction == EAgentRecoveryAction::Replan
            && Result.Counters.RepairAttempts == 0 && Executor.Count == 1,
        "Runtime preserves failure semantics and stops non-retryable tool failures immediately");
}

void TestConditionalReflectionAndRecoveryEscalation(FTestRunner& Runner)
{
    const auto MakeRepeatedRead = [](std::string Id)
    {
        return ToolCalls({{std::move(Id), "editor.world.describe", "{}"}});
    };

    {
        const auto Path = MakeLogPath("conditional-reflection-recovers");
        auto Session = Pico::FAgentSession::OpenOrCreate(
            "conditional-reflection-recovers", Path);
        FRecordingProvider Provider;
        Provider.Responses = {
            MakeRepeatedRead("read-1"),
            MakeRepeatedRead("read-2"),
            MakeRepeatedRead("read-3"),
            Final("Used the existing evidence and finished")};
        FCountingToolExecutor Executor;
        Executor.bReadOnly = true;
        Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
        const Pico::FAgentRunResult Result = Runtime.Run("describe the world once");

        bool bReflectionVisible = false;
        if (Provider.Requests.size() >= 4)
        {
            const std::string& Ledger = Provider.Requests[3].ProgressLedgerJson;
            bReflectionVisible = Ledger.find("\"active\":true")
                    != std::string::npos
                && Ledger.find("\"trigger\":\"action_oscillation\"")
                    != std::string::npos;
        }
        Runner.Expect(
            Result.Status == Pico::EAgentStatus::Completed
                && Result.Counters.ReflectionAttempts == 1
                && Result.Counters.RecoveryEscalations == 0
                && Executor.Count == 1 && bReflectionVisible,
            "A no-progress oscillation receives one structured reflection turn and can recover from cached evidence");
    }

    {
        const auto Path = MakeLogPath("conditional-reflection-escalates");
        auto Session = Pico::FAgentSession::OpenOrCreate(
            "conditional-reflection-escalates", Path);
        FRecordingProvider Provider;
        Provider.Responses = {
            MakeRepeatedRead("read-1"),
            MakeRepeatedRead("read-2"),
            MakeRepeatedRead("read-3"),
            MakeRepeatedRead("read-4"),
            Final("must not be reached")};
        FCountingToolExecutor Executor;
        Executor.bReadOnly = true;
        Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
        const Pico::FAgentRunResult Result = Runtime.Run("keep repeating the read");
        Runner.Expect(
            Result.Status == Pico::EAgentStatus::Failed
                && Result.Counters.ReflectionAttempts == 1
                && Result.Counters.RecoveryEscalations == 1
                && Result.Counters.OscillationsDetected >= 2,
            "A repeated failure after reflection escalates and stops instead of entering a reflection loop");
    }

    {
        const auto Path = MakeLogPath("conditional-reflection-disabled");
        auto Session = Pico::FAgentSession::OpenOrCreate(
            "conditional-reflection-disabled", Path);
        FRecordingProvider Provider;
        Provider.Responses = {
            MakeRepeatedRead("read-1"),
            MakeRepeatedRead("read-2"),
            MakeRepeatedRead("read-3")};
        FCountingToolExecutor Executor;
        Executor.bReadOnly = true;
        Pico::FAgentRuntimeContext Context;
        Context.Features.bConditionalReflection = false;
        Pico::FAgentRuntime Runtime(*Session, Provider, Executor, {}, Context);
        const Pico::FAgentRunResult Result = Runtime.Run("use the R4 baseline");
        Runner.Expect(
            Result.Status == Pico::EAgentStatus::Failed
                && Result.Counters.ReflectionAttempts == 0
                && Provider.Requests.size() == 3,
            "Disabling conditional reflection preserves the earlier ReAct stop behavior");
    }

    {
        const auto Path = MakeLogPath("conditional-reflection-checkpoint");
        auto Session = Pico::FAgentSession::OpenOrCreate(
            "conditional-reflection-checkpoint", Path);
        Pico::FAgentTaskState State;
        State.Goal = "resume reflected work";
        State.CurrentStep = "Conditional reflection and recovery";
        State.Revision = 2;
        Pico::FAgentCounters Counters;
        Counters.ReflectionAttempts = 1;
        std::string Error;
        Session->WriteCheckpoint(Pico::EAgentStatus::Planning, Counters,
            &Error, Pico::SerializeAgentTaskState(State));
        FRecordingProvider Provider;
        Provider.Responses = {Final("resumed without another reflection")};
        FCountingToolExecutor Executor;
        Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
        const Pico::FAgentRunResult Result = Runtime.Run("");
        const bool bRestoredDirective = !Provider.Requests.empty()
            && Provider.Requests.front().ProgressLedgerJson.find(
                "\"trigger\":\"checkpoint_resume\"") != std::string::npos;
        Runner.Expect(Error.empty()
                && Result.Status == Pico::EAgentStatus::Completed
                && Result.Counters.ReflectionAttempts == 1
                && bRestoredDirective,
            "Checkpoint recovery restores the pending reflection directive without granting a second attempt");
    }
}

void TestUnifiedTraceSpans(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("unified-trace");
    auto Session = Pico::FAgentSession::OpenOrCreate("unified-trace", Path);
    Pico::FAgentProviderResponse ToolResponse;
    ToolResponse.ToolCalls.push_back(
        {"trace-tool-call", "scene.fake", R"({"x":1})"});
    Pico::FFakeAgentProvider Provider(
        {{ToolResponse, {}}, {Final("trace complete"), {}}});
    FCountingToolExecutor Executor;
    Executor.bRequiresApproval = true;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const Pico::FAgentRunResult Result = Runtime.Run("trace this run");

    std::string RunSpanId;
    std::unordered_set<std::string> TurnSpanIds;
    std::unordered_set<std::string> ChildNames;
    bool bAllCorrelated = !Result.RunId.empty();
    for (const Pico::FAgentEvent& Event : Session->GetEvents())
    {
        if (Event.Type != Pico::EAgentEventType::TraceSpan) continue;
        bAllCorrelated &= Event.RunId == Result.RunId && !Event.SpanId.empty()
            && Event.StartedTimestampMilliseconds > 0;
        if (Event.SpanName == "AgentRun")
        {
            RunSpanId = Event.SpanId;
            bAllCorrelated &= Event.ParentSpanId.empty() && Event.TurnId.empty();
        }
        else if (Event.SpanName == "AgentTurn")
        {
            TurnSpanIds.insert(Event.SpanId);
            bAllCorrelated &= !Event.TurnId.empty();
        }
        else
        {
            ChildNames.insert(Event.SpanName);
            bAllCorrelated &= !Event.ParentSpanId.empty() && !Event.TurnId.empty();
        }
    }
    bool bTurnParentsAreRun = !RunSpanId.empty() && TurnSpanIds.size() == 2;
    bool bChildParentsAreTurns = true;
    for (const Pico::FAgentEvent& Event : Session->GetEvents())
    {
        if (Event.Type != Pico::EAgentEventType::TraceSpan) continue;
        if (Event.SpanName == "AgentTurn")
            bTurnParentsAreRun &= Event.ParentSpanId == RunSpanId;
        else if (Event.SpanName != "AgentRun")
            bChildParentsAreTurns &= TurnSpanIds.contains(Event.ParentSpanId);
    }
    Runner.Expect(
        Result.Status == Pico::EAgentStatus::Completed && bAllCorrelated
            && bTurnParentsAreRun && bChildParentsAreTurns
            && ChildNames.contains("Model.Generate")
            && ChildNames.contains("Tool.Approval")
            && ChildNames.contains("Tool.scene.fake")
            && ChildNames.contains("Run.Validation"),
        "Run, turn, model, approval, tool, and validation spans form one trace tree");

    auto Restored = Pico::FAgentSession::OpenOrCreate("unified-trace", Path);
    bool bRestoredTrace = false;
    for (const Pico::FAgentEvent& Event : Restored->GetEvents())
        bRestoredTrace |= Event.Type == Pico::EAgentEventType::TraceSpan
            && Event.RunId == Result.RunId && Event.SpanName == "AgentRun";
    Runner.Expect(bRestoredTrace,
        "Trace identifiers and span timing survive JSONL session recovery");

    std::ifstream MetricsStream(Result.MetricsPath);
    const std::string MetricsJson {
        std::istreambuf_iterator<char>(MetricsStream),
        std::istreambuf_iterator<char>()};
    Runner.Expect(
        Result.ContextBytes > 0 && !Result.MetricsPath.empty()
            && MetricsJson.find("\"format_version\": 1") != std::string::npos
            && MetricsJson.find("\"turns\": 2") != std::string::npos
            && MetricsJson.find("\"provider\"") != std::string::npos
            && MetricsJson.find("\"approval\"") != std::string::npos
            && MetricsJson.find("\"tool\"") != std::string::npos
            && MetricsJson.find("\"validation\"") != std::string::npos
            && MetricsJson.find("\"completion_rate\": 1.0")
                != std::string::npos,
        "Every Agent run persists versioned counts, latency, context, and completion metrics");
}

void TestToolCallIdempotency(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("idempotency");
    auto Session = Pico::FAgentSession::OpenOrCreate("idempotency", Path);
    Pico::FAgentProviderResponse First;
    First.ToolCalls.push_back({"stable-call", "scene.fake", R"({"x":1})"});
    Pico::FAgentProviderResponse Duplicate = First;
    Pico::FFakeAgentProvider Provider({{First, {}}, {Duplicate, {}}, {Final("done"), {}}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const Pico::FAgentRunResult Result = Runtime.Run("idempotent tool");

    bool bSawReusedResult = false;
    bool bSawPersistedTrace = false;
    for (const Pico::FAgentEvent& Event : Session->GetEvents())
    {
        bSawReusedResult |= Event.Type == Pico::EAgentEventType::ToolResult && Event.bReused;
        bSawPersistedTrace |= Event.Type == Pico::EAgentEventType::ToolResult
            && Event.TraceJson.find("Execute") != std::string::npos;
    }
    Runner.Expect(
        Result.Status == Pico::EAgentStatus::Completed && Executor.Count == 1
            && Result.Counters.ToolCalls == 1 && bSawReusedResult && bSawPersistedTrace,
        "A repeated stable ToolCall id reuses its result with zero duplicate side effects");

    auto Restored = Pico::FAgentSession::OpenOrCreate("idempotency", Path);
    Runner.Expect(
        Restored && Restored->FindToolResult("stable-call").has_value()
            && Restored->BuildMessageHistory().size() == 4,
        "Tool result idempotency cache survives restart without duplicating provider history");

    const std::uint64_t PreviousLastSequence =
        Session->GetEvents().empty() ? 0 : Session->GetEvents().back().Sequence;
    Pico::FAgentProviderResponse FreshTurnCall = First;
    Pico::FFakeAgentProvider FreshTurnProvider(
        {{FreshTurnCall, {}}, {Final("fresh done"), {}}});
    Pico::FAgentRuntime FreshTurnRuntime(
        *Session, FreshTurnProvider, Executor);
    const Pico::FAgentRunResult FreshTurnResult =
        FreshTurnRuntime.Run("new user turn with provider-reused call id");
    bool bFreshResultWasReused = false;
    for (const Pico::FAgentEvent& Event : Session->GetEvents())
        if (Event.Sequence > PreviousLastSequence
            && Event.Type == Pico::EAgentEventType::ToolResult)
            bFreshResultWasReused |= Event.bReused;
    Runner.Expect(FreshTurnResult.Status == Pico::EAgentStatus::Completed
            && Executor.Count == 2
            && FreshTurnResult.Counters.ToolCalls == 1
            && !bFreshResultWasReused,
        "A new user turn scopes ToolCall idempotency so provider-reused ids observe fresh editor state");
}

void TestBoundedRepairAndBudget(FTestRunner& Runner)
{
    auto RepairSession = Pico::FAgentSession::OpenOrCreate(
        "repair", MakeLogPath("repair"));
    Pico::FAgentProviderResponse Failure;
    Failure.bSucceeded = false;
    Failure.Error = "injected";
    Pico::FFakeAgentProvider RepairProvider({{Failure, {}}, {Final("repaired"), {}}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime RepairRuntime(*RepairSession, RepairProvider, Executor);
    const auto RepairResult = RepairRuntime.Run("repair once");
    Runner.Expect(
        RepairResult.Status == Pico::EAgentStatus::Completed
            && RepairResult.Counters.RepairAttempts == 1,
        "Provider failure enters Repairing and succeeds within the finite repair budget");

    auto BudgetSession = Pico::FAgentSession::OpenOrCreate(
        "budget", MakeLogPath("budget"));
    Pico::FAgentProviderResponse Empty;
    Pico::FFakeAgentProvider BudgetProvider({{Empty, {}}, {Empty, {}}});
    Pico::FAgentBudget Budget;
    Budget.MaxSteps = 1;
    Pico::FAgentRuntime BudgetRuntime(*BudgetSession, BudgetProvider, Executor, Budget);
    const auto BudgetResult = BudgetRuntime.Run("stop on budget");
    Runner.Expect(
        BudgetResult.Status == Pico::EAgentStatus::Failed
            && BudgetResult.Error == "Agent step budget exhausted",
        "Step budget stops an otherwise unbounded provider loop");

    auto ToolBudgetSession = Pico::FAgentSession::OpenOrCreate(
        "tool-budget", MakeLogPath("tool-budget"));
    Pico::FAgentProviderResponse ToolResponse;
    ToolResponse.ToolCalls.push_back({"over-budget", "scene.fake", "{}"});
    Pico::FFakeAgentProvider ToolBudgetProvider({{ToolResponse, {}}});
    FCountingToolExecutor ApprovalExecutor;
    ApprovalExecutor.bRequiresApproval = true;
    Pico::FAgentBudget ToolBudget;
    ToolBudget.MaxToolCalls = 0;
    Pico::FAgentRuntime ToolBudgetRuntime(
        *ToolBudgetSession, ToolBudgetProvider, ApprovalExecutor, ToolBudget);
    const auto ToolBudgetResult = ToolBudgetRuntime.Run("stop before approval");
    Runner.Expect(
        ToolBudgetResult.Status == Pico::EAgentStatus::Failed
            && ToolBudgetResult.Error
                == "Agent tool-call budget exhausted before approval"
            && ApprovalExecutor.PrepareApprovalCount == 0
            && ApprovalExecutor.Count == 0,
        "Tool-call budget is checked before approval and produces zero side effects");
}

void TestSemanticReadCacheAndNoProgressGuard(FTestRunner& Runner)
{
    auto Session = Pico::FAgentSession::OpenOrCreate(
        "semantic-cache", MakeLogPath("semantic-cache"));
    Pico::FAgentProviderResponse FirstRead;
    FirstRead.ToolCalls.push_back(
        {"read-1", "scene.describe", R"({"b":2,"a":1})"});
    Pico::FAgentProviderResponse EquivalentRead;
    EquivalentRead.ToolCalls.push_back(
        {"read-2", "scene.describe", R"({"a":1,"b":2})"});
    FRecordingProvider Provider;
    Provider.Responses = {FirstRead, EquivalentRead, Final("done")};
    FCountingToolExecutor Executor;
    Executor.bReadOnly = true;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const Pico::FAgentRunResult Result = Runtime.Run("inspect once, then finish");

    const auto CachedResult = Session->FindToolResult("read-2");
    Runner.Expect(Result.Status == Pico::EAgentStatus::Completed
            && Executor.Count == 1 && Result.Counters.ToolCalls == 1
            && Result.Counters.ReadOnlyToolCalls == 1
            && Result.Counters.SemanticCacheHits == 1 && CachedResult
            && CachedResult->bReused
            && CachedResult->OutputJson.find("semantic_cache_hit")
                != std::string::npos,
        "Equivalent read-only calls reuse one semantic result across different CallIds");
    Runner.Expect(Provider.Requests.size() == 3
            && Provider.Requests.back().ProgressLedgerJson.find(
                "inspect once, then finish") != std::string::npos
            && Provider.Requests.back().ProgressLedgerJson.find(
                "semantic_cache_hits") != std::string::npos,
        "Every provider step receives a structured progress and budget ledger");

    auto RevisionSession = Pico::FAgentSession::OpenOrCreate(
        "cache-revision", MakeLogPath("cache-revision"));
    Pico::FAgentProviderResponse RevisionReadA;
    RevisionReadA.ToolCalls.push_back(
        {"revision-read-1", "scene.describe", "{}"});
    Pico::FAgentProviderResponse RevisionMutation;
    RevisionMutation.ToolCalls.push_back(
        {"revision-mutate", "scene.change", "{}"});
    Pico::FAgentProviderResponse RevisionReadB;
    RevisionReadB.ToolCalls.push_back(
        {"revision-read-2", "scene.describe", "{}"});
    Pico::FFakeAgentProvider RevisionProvider({{RevisionReadA, {}},
        {RevisionMutation, {}}, {RevisionReadB, {}}, {Final("done"), {}}});
    FCountingToolExecutor RevisionExecutor;
    RevisionExecutor.ReadOnlyToolName = "scene.describe";
    Pico::FAgentRuntime RevisionRuntime(
        *RevisionSession, RevisionProvider, RevisionExecutor);
    const Pico::FAgentRunResult RevisionResult =
        RevisionRuntime.Run("read, change, and read again");
    Runner.Expect(RevisionResult.Status == Pico::EAgentStatus::Completed
            && RevisionExecutor.Count == 3
            && RevisionResult.Counters.ReadOnlyToolCalls == 2
            && RevisionResult.Counters.MutationToolCalls == 1
            && RevisionResult.Counters.SemanticCacheHits == 0,
        "A successful mutation advances StateRevision and invalidates read cache keys");

    auto DomainSession = Pico::FAgentSession::OpenOrCreate(
        "cache-domain", MakeLogPath("cache-domain"));
    Pico::FFakeAgentProvider DomainProvider({
        {ToolCalls({{"asset-read-1", "asset.describe", "{}"}}), {}},
        {ToolCalls({{"graph-write", "graph.change", "{}"}}), {}},
        {ToolCalls({{"asset-read-2", "asset.describe", "{}"}}), {}},
        {Final("done"), {}}});
    FCountingToolExecutor DomainExecutor;
    DomainExecutor.ReadOnlyToolName = "asset.describe";
    DomainExecutor.ReadSets["asset.describe"] = {"Asset.Revision"};
    DomainExecutor.WriteSets["graph.change"] = {"Graph.Revision"};
    Pico::FAgentRuntime DomainRuntime(
        *DomainSession, DomainProvider, DomainExecutor);
    const Pico::FAgentRunResult DomainResult =
        DomainRuntime.Run("change graph without invalidating asset cache");
    Runner.Expect(DomainResult.Status == Pico::EAgentStatus::Completed
            && DomainExecutor.Count == 2
            && DomainResult.Counters.SemanticCacheHits == 1,
        "Graph revision changes do not invalidate unrelated Asset reads");
    const auto PersistedRevisions = DomainSession->BuildRevisionSnapshot();
    Runner.Expect(PersistedRevisions.contains("Graph.Revision")
            && PersistedRevisions.at("Graph.Revision") == 1,
        "Domain revisions rebuild from durable Tool Results across turns");

    auto ContextSession = Pico::FAgentSession::OpenOrCreate(
        "bounded-context", MakeLogPath("bounded-context"));
    for (int Index = 0; Index < 10; ++Index)
    {
        Pico::FAgentEvent Message;
        Message.Type = Pico::EAgentEventType::Message;
        Message.Role = Index % 2 == 0
            ? Pico::EAgentRole::User : Pico::EAgentRole::Assistant;
        Message.Content = "history-" + std::to_string(Index);
        ContextSession->Append(std::move(Message));
    }
    std::size_t TrimmedMessages = 0;
    const auto BoundedHistory = ContextSession->BuildBoundedMessageHistory(
        4, 1024, &TrimmedMessages);
    Runner.Expect(BoundedHistory.size() == 4 && TrimmedMessages == 6
            && BoundedHistory.front().Content == "history-6"
            && BoundedHistory.back().Content == "history-9",
        "Agent context retains a bounded newest-message window");
    const auto ByteBoundedHistory = ContextSession->BuildBoundedMessageHistory(
        10, 5, &TrimmedMessages);
    Runner.Expect(ByteBoundedHistory.empty() && TrimmedMessages == 10,
        "Agent context byte budget is a hard upper bound");

    auto ProtocolSession = Pico::FAgentSession::OpenOrCreate(
        "bounded-tool-protocol", MakeLogPath("bounded-tool-protocol"));
    Pico::FAgentEvent OldUser;
    OldUser.Type = Pico::EAgentEventType::Message;
    OldUser.Role = Pico::EAgentRole::User;
    OldUser.Content = "inspect first";
    ProtocolSession->Append(std::move(OldUser));
    Pico::FAgentEvent Assistant;
    Assistant.Type = Pico::EAgentEventType::Message;
    Assistant.Role = Pico::EAgentRole::Assistant;
    Assistant.Content = "I will inspect";
    ProtocolSession->Append(std::move(Assistant));
    Pico::FAgentEvent ToolCall;
    ToolCall.Type = Pico::EAgentEventType::ToolCall;
    ToolCall.CallId = "protocol-call";
    ToolCall.ToolName = "scene.describe";
    ProtocolSession->Append(std::move(ToolCall));
    Pico::FAgentEvent ToolResult;
    ToolResult.Type = Pico::EAgentEventType::ToolResult;
    ToolResult.Role = Pico::EAgentRole::Tool;
    ToolResult.CallId = "protocol-call";
    ToolResult.ToolName = "scene.describe";
    ToolResult.bSucceeded = true;
    ToolResult.PayloadJson = R"({"world":"test"})";
    ProtocolSession->Append(std::move(ToolResult));
    Pico::FAgentEvent NewUser;
    NewUser.Type = Pico::EAgentEventType::Message;
    NewUser.Role = Pico::EAgentRole::User;
    NewUser.Content = "describe again";
    ProtocolSession->Append(std::move(NewUser));

    const auto SplitHistory = ProtocolSession->BuildBoundedMessageHistory(
        2, 4096, &TrimmedMessages);
    Runner.Expect(SplitHistory.size() == 1
            && SplitHistory.front().Role == Pico::EAgentRole::User
            && SplitHistory.front().Content == "describe again",
        "Context trimming removes orphan Tool Results when a tool group is split");
    const auto CompleteToolHistory = ProtocolSession->BuildBoundedMessageHistory(
        3, 4096, &TrimmedMessages);
    Runner.Expect(CompleteToolHistory.size() == 3
            && CompleteToolHistory[0].Role == Pico::EAgentRole::Assistant
            && CompleteToolHistory[0].ToolCalls.size() == 1
            && CompleteToolHistory[1].Role == Pico::EAgentRole::Tool
            && CompleteToolHistory[1].ToolCallId == "protocol-call",
        "Context trimming retains complete Assistant ToolCall and Tool Result groups");

    auto LoopSession = Pico::FAgentSession::OpenOrCreate(
        "no-progress", MakeLogPath("no-progress"));
    Pico::FAgentProviderResponse LoopReadA;
    LoopReadA.ToolCalls.push_back({"loop-1", "scene.describe", "{}"});
    Pico::FAgentProviderResponse LoopReadB;
    LoopReadB.ToolCalls.push_back({"loop-2", "scene.describe", "{}"});
    Pico::FAgentProviderResponse LoopReadC;
    LoopReadC.ToolCalls.push_back({"loop-3", "scene.describe", "{}"});
    Pico::FFakeAgentProvider LoopProvider(
        {{LoopReadA, {}}, {LoopReadB, {}}, {LoopReadC, {}}});
    FCountingToolExecutor LoopExecutor;
    LoopExecutor.bReadOnly = true;
    Pico::FAgentBudget LoopBudget;
    LoopBudget.MaxConsecutiveNoProgressSteps = 2;
    Pico::FAgentRuntimeContext LoopContext;
    LoopContext.Features.bConditionalReflection = false;
    Pico::FAgentRuntime LoopRuntime(
        *LoopSession, LoopProvider, LoopExecutor, LoopBudget, LoopContext);
    const Pico::FAgentRunResult LoopResult = LoopRuntime.Run("do not loop");
    Runner.Expect(LoopResult.Status == Pico::EAgentStatus::Failed
            && LoopExecutor.Count == 1
            && LoopResult.Counters.SemanticCacheHits == 2
            && LoopResult.Error.find("made no progress") != std::string::npos,
        "Two repeated no-progress query steps stop before exhausting the global budget");
}

void TestReActTaskStateAndContextAssembler(FTestRunner& Runner)
{
    Pico::FAgentTaskState OriginalState;
    OriginalState.Goal = "Inspect the world without changing it";
    OriginalState.SuccessCriteria = {"World facts are cited"};
    OriginalState.Constraints = {"No mutations"};
    OriginalState.CurrentStep = "Read the current World";
    OriginalState.RemainingSteps = {"Summarize verified facts"};
    OriginalState.EvidenceRefs = {"tool-result:existing"};
    OriginalState.OpenQuestions = {"Which map is active?"};
    OriginalState.Revision = 7;
    const std::string StateJson = Pico::SerializeAgentTaskState(OriginalState);
    Pico::FAgentTaskState RestoredState;
    std::string Error;
    Runner.Expect(Pico::DeserializeAgentTaskState(
            StateJson, RestoredState, &Error)
            && RestoredState.Goal == OriginalState.Goal
            && RestoredState.SuccessCriteria == OriginalState.SuccessCriteria
            && RestoredState.Constraints == OriginalState.Constraints
            && RestoredState.Revision == 7,
        "Task State has a versioned deterministic JSON round trip");

    Pico::FAgentContextAssemblyInput AssemblyInput;
    for (int Index = 0; Index < 8; ++Index)
    {
        AssemblyInput.Messages.push_back({Index % 2 == 0
            ? Pico::EAgentRole::User : Pico::EAgentRole::Assistant,
            "message-" + std::to_string(Index)});
    }
    AssemblyInput.TaskStateJson = StateJson;
    AssemblyInput.ObservationContextJson = R"({"revision":4})";
    AssemblyInput.KnowledgeContextJson =
        std::string("{\"large\":\"") + std::string(2048, 'k') + "\"}";
    AssemblyInput.SkillContextJson = R"([{"id":"inspect"}])";
    AssemblyInput.MaxMessages = 4;
    AssemblyInput.MaxBytes = 1024;
    const Pico::FAgentAssembledContext Assembled =
        Pico::FAgentContextAssembler::Assemble(std::move(AssemblyInput));
    Runner.Expect(Assembled.Messages.size() == 4
            && Assembled.TrimmedMessages == 4
            && Assembled.Metrics.AssemblyCount == 1
            && Assembled.Metrics.TotalBytes <= 1024
            && Assembled.Metrics.TaskStateBytes > 0
            && Assembled.Metrics.ConversationBytes > 0
            && Assembled.Metrics.DroppedBytes > 0
            && Assembled.KnowledgeContextJson == "{}",
        "Context Assembler reserves conversation space and drops oversized low-priority evidence");

    const auto Path = MakeLogPath("react-r1-context");
    auto Session = Pico::FAgentSession::OpenOrCreate(
        "react-r1-context", Path);
    FRecordingProvider Provider;
    Provider.Responses = {Final("done")};
    FCountingToolExecutor Executor;
    Pico::FAgentRuntimeContext RuntimeContext;
    RuntimeContext.KnowledgeContextJson = R"({"facts":[{"id":"world"}]})";
    RuntimeContext.SkillContextJson = R"([{"id":"inspect-world"}])";
    Pico::FAgentRuntime Runtime(
        *Session, Provider, Executor, {}, std::move(RuntimeContext));
    const Pico::FAgentRunResult Result =
        Runtime.Run("Describe the current world");
    Runner.Expect(Result.Status == Pico::EAgentStatus::Completed
            && Provider.Requests.size() == 1
            && Provider.Requests.front().TaskStateJson.find(
                "Describe the current world") != std::string::npos
            && Result.ContextMetrics.AssemblyCount == 1
            && Result.ContextMetrics.MaxAssemblyMicroseconds < 5000
            && Result.ContextMetrics.TotalBytes == Result.ContextBytes,
        "A simple ReAct task uses one model turn and assembles context below the 5 ms gate");

    auto RestoredSession = Pico::FAgentSession::OpenOrCreate(
        "react-r1-context", Path);
    Pico::FAgentTaskState PersistedState;
    Runner.Expect(RestoredSession
            && Pico::DeserializeAgentTaskState(
                RestoredSession->GetLatestTaskStateJson(), PersistedState)
            && PersistedState.Goal == "Describe the current world",
        "Task State survives a JSONL checkpoint and session restart");

    std::ifstream MetricsStream(Result.MetricsPath);
    const std::string MetricsJson {
        std::istreambuf_iterator<char>(MetricsStream),
        std::istreambuf_iterator<char>()};
    Runner.Expect(MetricsJson.find("\"context_assembly\"")
            != std::string::npos
            && MetricsJson.find("\"task_state_bytes\"")
                != std::string::npos,
        "Run metrics persist Context Assembler latency and partition sizes");

    auto LegacySession = Pico::FAgentSession::OpenOrCreate(
        "react-r1-legacy", MakeLogPath("react-r1-legacy"));
    FRecordingProvider LegacyProvider;
    LegacyProvider.Responses = {Final("legacy done")};
    Pico::FAgentRuntimeContext LegacyContext;
    LegacyContext.Features.bTaskState = false;
    LegacyContext.Features.bContextAssembler = false;
    LegacyContext.Features.bContextMetrics = false;
    Pico::FAgentRuntime LegacyRuntime(
        *LegacySession, LegacyProvider, Executor, {}, std::move(LegacyContext));
    const Pico::FAgentRunResult LegacyResult = LegacyRuntime.Run("legacy path");
    Runner.Expect(LegacyResult.Status == Pico::EAgentStatus::Completed
            && LegacyProvider.Requests.size() == 1
            && LegacyProvider.Requests.front().TaskStateJson == "{}"
            && LegacyResult.ContextMetrics.AssemblyCount == 0,
        "Feature flags restore the pre-R1 context path without an extra model turn");
}

void TestReActObservationsEvidenceAndOscillation(FTestRunner& Runner)
{
    Pico::FAgentToolCall Call {
        "describe-world", "editor.world.describe", R"({"b":2,"a":1})"};
    Pico::FAgentToolResult ToolResult {
        Call.Id, true, R"({"actors":3})", {}, false};
    Pico::NormalizeAgentToolResult(ToolResult);
    const Pico::FAgentObservation Observation = Pico::BuildAgentObservation(
        Call, ToolResult, true, true);
    Pico::FAgentTaskState State;
    State.Goal = "Describe the world";
    State.SuccessCriteria = {"World facts are supported"};
    State.CriterionEvidence = {{State.SuccessCriteria.front(), {}, false}};
    Pico::BindAgentObservationEvidence(Observation, State);
    Runner.Expect(Observation.bVerified && Observation.bMadeProgress
            && Observation.ActionFingerprint
                == Pico::BuildAgentActionFingerprint({"different-id",
                    "editor.world.describe", R"({"a":1,"b":2})"})
            && Pico::HasAgentCompletionEvidence(State)
            && State.CriterionEvidence.front().EvidenceRefs.front()
                == "observation:describe-world",
        "Observation mapping canonicalizes actions and binds verified evidence to success criteria");
    Runner.Expect(Pico::SerializeAgentObservation(Observation).find(
            "\"revision_changes\"") != std::string::npos,
        "Observation serialization exposes structured facts and progress metadata");

    class FEmptySuccessExecutor final : public Pico::IAgentToolExecutor
    {
    public:
        bool IsReadOnly(const Pico::FAgentToolCall&) const override
        {
            return true;
        }

        Pico::FAgentToolResult Execute(const Pico::FAgentToolCall& Call,
            const Pico::FCancellationToken*) override
        {
            return {Call.Id, true, "{}", {}, false};
        }
    } EmptyExecutor;
    auto EmptySession = Pico::FAgentSession::OpenOrCreate(
        "react-r2-empty-evidence", MakeLogPath("react-r2-empty-evidence"));
    FRecordingProvider EmptyProvider;
    EmptyProvider.Responses = {
        ToolCalls({{"empty-success", "scene.fake", "{}"}}),
        Final("completed")};
    Pico::FAgentRuntimeContext R2BaselineContext;
    R2BaselineContext.Features.bConditionalReflection = false;
    Pico::FAgentRuntime EmptyRuntime(
        *EmptySession, EmptyProvider, EmptyExecutor, {}, R2BaselineContext);
    const Pico::FAgentRunResult EmptyResult = EmptyRuntime.Run(
        "Perform and verify the requested change");
    bool bPersistedUnsupportedClaim = false;
    for (const Pico::FAgentEvent& Event : EmptySession->GetEvents())
        bPersistedUnsupportedClaim |= Event.Type == Pico::EAgentEventType::Message
            && Event.Role == Pico::EAgentRole::Assistant
            && Event.Content == "completed";
    Runner.Expect(EmptyResult.Status == Pico::EAgentStatus::Failed
            && EmptyResult.FailureClass
                == Pico::EAgentFailureClass::VerificationFailed
            && EmptyResult.Error.find("no verified evidence")
                != std::string::npos && !bPersistedUnsupportedClaim,
        "A successful tool status without facts, artifacts, or revisions cannot justify completion");

    auto ReadbackSession = Pico::FAgentSession::OpenOrCreate(
        "react-r2-mutation-readback", MakeLogPath("react-r2-mutation-readback"));
    FRecordingProvider ReadbackProvider;
    ReadbackProvider.Responses = {
        ToolCalls({{"spawn-empty-light", "editor.actor.spawn",
            R"({"name":"VisualLight","kind":"Empty"})"}}),
        Final("The blue point light is complete"),
        ToolCalls({{"describe-light", "editor.object.describe",
            R"({"object_path":"World.VisualLight"})"}}),
        Final("The Actor was read back and verified")};
    FCountingToolExecutor ReadbackExecutor;
    ReadbackExecutor.ReadOnlyToolName = "editor.object.describe";
    Pico::FAgentRuntimeContext ReadbackContext;
    ReadbackContext.Features.bMutationReadbackGate = true;
    Pico::FAgentRuntime ReadbackRuntime(
        *ReadbackSession, ReadbackProvider, ReadbackExecutor, {}, ReadbackContext);
    const Pico::FAgentRunResult ReadbackResult = ReadbackRuntime.Run(
        "Create a blue point light");
    bool bPersistedPrematureClaim = false;
    for (const Pico::FAgentEvent& Event : ReadbackSession->GetEvents())
        bPersistedPrematureClaim |= Event.Type == Pico::EAgentEventType::Message
            && Event.Role == Pico::EAgentRole::Assistant
            && Event.Content == "The blue point light is complete";
    Runner.Expect(ReadbackResult.Status == Pico::EAgentStatus::Completed
            && ReadbackResult.Counters.ReflectionAttempts == 1
            && ReadbackExecutor.Count == 2
            && ReadbackProvider.Requests.size() == 4
            && ReadbackProvider.Requests[2].ProgressLedgerJson.find(
                "\"mutation_readback_pending\":true") != std::string::npos
            && ReadbackProvider.Requests[3].ProgressLedgerJson.find(
                "\"mutation_readback_pending\":false") != std::string::npos
            && !bPersistedPrematureClaim,
        "A mutation cannot be reported as complete until a fresh read-only inspection runs");

    auto RestartSession = Pico::FAgentSession::OpenOrCreate(
        "react-r2-restart-gate", MakeLogPath("react-r2-restart-gate"));
    Pico::FAgentTaskState RestartState;
    RestartState.Goal = "Verify after restart";
    RestartState.SuccessCriteria = {"Verified evidence exists"};
    RestartState.CriterionEvidence = {
        {RestartState.SuccessCriteria.front(), {}, false}};
    RestartState.ObservationCount = 1;
    RestartState.Revision = 2;
    Pico::FAgentCounters RestartCounters;
    std::string RestartError;
    RestartSession->WriteCheckpoint(Pico::EAgentStatus::Planning,
        RestartCounters, &RestartError,
        Pico::SerializeAgentTaskState(RestartState));
    FRecordingProvider RestartProvider;
    RestartProvider.Responses = {Final("unsupported completion after restart")};
    Pico::FAgentRuntime RestartRuntime(
        *RestartSession, RestartProvider, EmptyExecutor, {}, R2BaselineContext);
    const Pico::FAgentRunResult RestartResult = RestartRuntime.Run("");
    Runner.Expect(RestartError.empty()
            && RestartResult.Status == Pico::EAgentStatus::Failed
            && RestartResult.FailureClass
                == Pico::EAgentFailureClass::VerificationFailed,
        "Checkpoint recovery preserves tool activity and cannot bypass the evidence gate");

    auto AlternatingSession = Pico::FAgentSession::OpenOrCreate(
        "react-r2-oscillation", MakeLogPath("react-r2-oscillation"));
    FRecordingProvider AlternatingProvider;
    AlternatingProvider.Responses = {
        ToolCalls({{"read-a-1", "scene.describe", R"({"page":1})"}}),
        ToolCalls({{"read-b-1", "scene.describe", R"({"page":2})"}}),
        ToolCalls({{"read-a-2", "scene.describe", R"({"page":1})"}}),
        ToolCalls({{"read-b-2", "scene.describe", R"({"page":2})"}})};
    FCountingToolExecutor AlternatingExecutor;
    AlternatingExecutor.bReadOnly = true;
    Pico::FAgentRuntime AlternatingRuntime(
        *AlternatingSession, AlternatingProvider, AlternatingExecutor, {},
        R2BaselineContext);
    const Pico::FAgentRunResult AlternatingResult = AlternatingRuntime.Run(
        "Inspect two pages without oscillating");
    Runner.Expect(AlternatingResult.Status == Pico::EAgentStatus::Failed
            && AlternatingResult.Counters.OscillationsDetected == 1
            && AlternatingResult.Counters.SemanticCacheHits == 2
            && AlternatingExecutor.Count == 2
            && AlternatingResult.Error.find("A-B-A") != std::string::npos,
        "A-B-A action oscillation stops at an unchanged revision without repeating handlers");

    auto DistinctSession = Pico::FAgentSession::OpenOrCreate(
        "react-r2-distinct-reads", MakeLogPath("react-r2-distinct-reads"));
    FRecordingProvider DistinctProvider;
    DistinctProvider.Responses = {
        ToolCalls({{"distinct-a", "scene.describe", R"({"page":1})"}}),
        ToolCalls({{"distinct-b", "scene.describe", R"({"page":2})"}}),
        Final("two distinct pages inspected")};
    FCountingToolExecutor DistinctExecutor;
    DistinctExecutor.bReadOnly = true;
    Pico::FAgentRuntime DistinctRuntime(
        *DistinctSession, DistinctProvider, DistinctExecutor);
    const Pico::FAgentRunResult DistinctResult = DistinctRuntime.Run(
        "Inspect two distinct pages");
    Runner.Expect(DistinctResult.Status == Pico::EAgentStatus::Completed
            && DistinctResult.Counters.OscillationsDetected == 0
            && DistinctResult.Counters.Observations == 2
            && DistinctResult.Counters.EvidenceBindings == 2
            && DistinctExecutor.Count == 2
            && DistinctProvider.Requests.back().ProgressLedgerJson.find(
                "latest_observations") != std::string::npos,
        "Different read actions remain legal and expose bounded observations to the next turn");
}

void TestCategorizedBudgetBeforeSideEffects(FTestRunner& Runner)
{
    auto Session = Pico::FAgentSession::OpenOrCreate(
        "read-budget", MakeLogPath("read-budget"));
    Pico::FAgentProviderResponse Reads;
    Reads.ToolCalls.push_back({"read-a", "scene.describe", R"({"page":1})"});
    Reads.ToolCalls.push_back({"read-b", "scene.describe", R"({"page":2})"});
    Pico::FFakeAgentProvider Provider({{Reads, {}}});
    FCountingToolExecutor Executor;
    Executor.bReadOnly = true;
    Pico::FAgentBudget Budget;
    Budget.MaxReadOnlyToolCalls = 1;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor, Budget);
    const Pico::FAgentRunResult Result = Runtime.Run("bounded reads");
    Runner.Expect(Result.Status == Pico::EAgentStatus::Failed
            && Result.Error == "Agent read-only tool budget exhausted before approval"
            && Executor.Count == 0,
        "Read-only category budget rejects a batch before approval or side effects");
}

void TestProjectHandoffIsOneShotAndCredentialFree(FTestRunner& Runner)
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / "PicoAgentTests" / "ProjectHandoff";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    const std::filesystem::path SourceProject = Root / "Source/Source.pico";
    const std::filesystem::path TargetProject = Root / "Target/Target.pico";
    const std::filesystem::path SourceSession =
        Root / "Source/Saved/Agent/Sessions/editor-chat-deepseek-test.jsonl";
    std::filesystem::create_directories(SourceSession.parent_path());
    std::filesystem::create_directories(TargetProject.parent_path());
    { std::ofstream(SourceProject) << "[Project]\nName=Source\n"; }
    { std::ofstream(TargetProject) << "[Project]\nName=Target\n"; }
    { std::ofstream(SourceSession) << "{\"session\":true}\n"; }
    std::filesystem::create_directories(
        SourceProject.parent_path() / "Saved/Agent");
    { std::ofstream(SourceProject.parent_path()
        / "Saved/Agent/ApiKeys.ini") << "must-not-copy"; }

    Pico::FAgentProjectHandoff Handoff;
    Handoff.SourceProjectFile = SourceProject;
    Handoff.TargetProjectFile = TargetProject;
    Handoff.SessionPath = SourceSession;
    Handoff.SessionId = "editor-chat-deepseek-test";
    Handoff.Provider = "deepseek";
    Handoff.Model = "test-model";
    Handoff.Goal = "continue in the target project";
    std::string HandoffError;
    Pico::FAgentProjectHandoff UnsafeHandoff = Handoff;
    UnsafeHandoff.SessionId = "../escape";
    Runner.Expect(!Pico::PrepareAgentProjectHandoff(
            UnsafeHandoff, &HandoffError),
        "Project handoff rejects a session id that could escape its target directory");
    HandoffError.clear();
    const bool bPrepared = Pico::PrepareAgentProjectHandoff(
        Handoff, &HandoffError);
    std::string ConsumeError;
    const auto Consumed = Pico::ConsumeAgentProjectHandoff(
        TargetProject.parent_path(), &ConsumeError);
    const auto ConsumedAgain = Pico::ConsumeAgentProjectHandoff(
        TargetProject.parent_path(), &HandoffError);
    Runner.Expect(bPrepared,
        "Project handoff publishes a manifest after copying the source session");
    Runner.Expect(Consumed.has_value(),
        "Target project consumes a valid handoff manifest");
    Runner.Expect(Consumed && Consumed->SessionId == Handoff.SessionId
            && Consumed->Provider == "deepseek",
        "Consumed handoff restores the selected session and Provider");
    Runner.Expect(Consumed
            && std::filesystem::is_regular_file(Consumed->SessionPath),
        "Consumed handoff points at the copied target-project session");
    Runner.Expect(!ConsumedAgain
            && !std::filesystem::exists(TargetProject.parent_path()
                / "Saved/Agent/ProjectHandoff.json"),
        "Project handoff manifest is consumed exactly once");
    Runner.Expect(!std::filesystem::exists(TargetProject.parent_path()
            / "Saved/Agent/ApiKeys.ini"),
        "Project handoff never copies the source project's local API key");
    std::filesystem::remove_all(Root, Error);
}

void TestCheckpointResume(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("resume");
    auto Session = Pico::FAgentSession::OpenOrCreate("resume", Path);
    Pico::FAgentEvent Message;
    Message.Type = Pico::EAgentEventType::Message;
    Message.Role = Pico::EAgentRole::User;
    Message.Content = "survive restart";
    Session->Append(std::move(Message));
    Pico::FAgentCounters BeforeRestart;
    BeforeRestart.Steps = 2;
    Session->WriteCheckpoint(Pico::EAgentStatus::Planning, BeforeRestart);

    auto Restored = Pico::FAgentSession::OpenOrCreate("resume", Path);
    Pico::FFakeAgentProvider Provider({{Final("resumed"), {}}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime Runtime(*Restored, Provider, Executor);
    const auto Result = Runtime.Run("");
    Runner.Expect(
        Result.Status == Pico::EAgentStatus::Completed
            && Result.Counters.Steps == 3
            && Restored->BuildMessageHistory().front().Content == "survive restart",
        "A new runtime resumes an incomplete session from its last durable checkpoint");
}

void TestIncompleteTailRecoveryAndStateRules(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("incomplete-tail");
    auto Session = Pico::FAgentSession::OpenOrCreate("incomplete-tail", Path);
    Pico::FAgentEvent Message;
    Message.Type = Pico::EAgentEventType::Message;
    Message.Role = Pico::EAgentRole::User;
    Message.Content = "durable event";
    Session->Append(std::move(Message));
    {
        std::ofstream Stream(Path, std::ios::binary | std::ios::app);
        Stream << "{\"interrupted\":";
    }
    auto Restored = Pico::FAgentSession::OpenOrCreate("incomplete-tail", Path);
    Pico::FAgentEvent AfterRecovery;
    AfterRecovery.Type = Pico::EAgentEventType::Message;
    AfterRecovery.Role = Pico::EAgentRole::Assistant;
    AfterRecovery.Content = "append still works";
    const bool bAppended = Restored && Restored->Append(std::move(AfterRecovery));
    auto Reopened = Pico::FAgentSession::OpenOrCreate("incomplete-tail", Path);
    Runner.Expect(
        bAppended && Reopened && Reopened->BuildMessageHistory().size() == 2,
        "Recovery truncates only an incomplete final JSONL append and remains appendable");
    Runner.Expect(
        Pico::IsAllowedAgentTransition(Pico::EAgentStatus::Planning,
            Pico::EAgentStatus::ExecutingTool)
            && !Pico::IsAllowedAgentTransition(Pico::EAgentStatus::ExecutingTool,
                Pico::EAgentStatus::Completed),
        "Agent state machine accepts declared edges and rejects skipped validation");
}

void TestCooperativeCancellation(FTestRunner& Runner)
{
    auto Session = Pico::FAgentSession::OpenOrCreate(
        "cancel", MakeLogPath("cancel"));
    Pico::FFakeAgentProvider Provider({{Final("too late"), 500ms}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    Pico::FTaskSystem Tasks;
    Tasks.Initialize(1);
    Pico::FAgentRunResult RunResult;
    std::atomic<bool> Started {false};
    Pico::FTaskHandle Handle = Tasks.Submit(
        "Cancelable fake agent",
        [&](const Pico::FCancellationToken& Token)
        {
            Started.store(true);
            RunResult = Runtime.Run("cancel me", &Token);
        });
    while (!Started.load()) std::this_thread::sleep_for(1ms);
    Handle.RequestCancel();
    Handle.Wait(2s);
    Runner.Expect(
        Handle.GetState() == Pico::ETaskState::Cancelled
            && RunResult.Status == Pico::EAgentStatus::Cancelled
            && Session->GetStatus() == Pico::EAgentStatus::Cancelled,
        "PicoTask cancellation propagates through provider wait and persists Cancelled state");
    Tasks.Shutdown();
}

void TestToolPipelineRejectsBeforeSideEffects(FTestRunner& Runner)
{
    int SideEffect = 0;
    int HandlerCalls = 0;
    FTestApproval Approval;
    FTestTransaction Transaction(SideEffect);
    Pico::FAgentToolPolicy Policy;
    Policy.bAllowModifyWorld = true;
    Policy.bAllowWriteProject = true;
    Policy.ProjectRoot = std::filesystem::temp_directory_path() / "PicoAgentProject";
    std::filesystem::create_directories(Policy.ProjectRoot);
    Pico::FAgentToolRegistry Registry(Policy, &Approval, &Transaction);

    Pico::FAgentToolDefinition Tool;
    Tool.Name = "test.modify";
    Tool.Description = "Modify deterministic test state";
    Tool.Permission = Pico::EAgentToolPermission::ModifyWorld;
    Tool.Schema.Fields = {
        {"amount", Pico::EAgentToolValueType::Integer, true, 1.0, 10.0},
        {"path", Pico::EAgentToolValueType::String, false, {}, {}, 128,
            Pico::EAgentToolStringFormat::ProjectRelativePath}
    };
    Tool.Handler = [&](const Pico::FAgentToolCall& Call, const Pico::FCancellationToken*)
    {
        ++HandlerCalls;
        SideEffect += 5;
        return Pico::FAgentToolResult {Call.Id, true, "{}", {}, false};
    };
    Tool.Verifier = [](const Pico::FAgentToolCall&, const Pico::FAgentToolResult&, std::string&)
    {
        return true;
    };
    Runner.Expect(
        Registry.Register(std::move(Tool))
            && Registry.BuildToolCatalogJson().find("test.modify") != std::string::npos
            && Registry.BuildToolCatalogJson().find("additionalProperties") != std::string::npos,
        "Tool registry exposes a deterministic provider-facing JSON schema catalog");

    const auto WrongType = Registry.Execute(
        {"wrong-type", "test.modify", R"({"amount":"five"})"}, nullptr);
    const auto EscapedPath = Registry.Execute(
        {"escape", "test.modify", R"({"amount":5,"path":"../outside.txt"})"}, nullptr);
    Runner.Expect(
        !WrongType.bSucceeded && !EscapedPath.bSucceeded
            && WrongType.FailureClass == Pico::EAgentFailureClass::InvalidArguments
            && HandlerCalls == 0 && Transaction.BeginCount == 0
            && Approval.RequestCount == 0 && SideEffect == 0,
        "Wrong types and project path traversal fail before approval, transaction, or handler");

    Approval.bApprove = false;
    const Pico::FAgentToolCall Denied {"denied", "test.modify", R"({"amount":5})"};
    Registry.PrepareApproval(Denied);
    const auto DeniedResult = Registry.Execute(Denied, nullptr);
    Runner.Expect(
        !DeniedResult.bSucceeded && HandlerCalls == 0
            && DeniedResult.FailureClass == Pico::EAgentFailureClass::ApprovalRejected
            && Transaction.BeginCount == 0 && SideEffect == 0,
        "User denial has zero side effects and never opens a transaction");

    Pico::FAgentToolDefinition WriteTool;
    WriteTool.Name = "test.write";
    WriteTool.Description = "Write a deterministic project file";
    WriteTool.Permission = Pico::EAgentToolPermission::WriteProject;
    WriteTool.Handler = [&](const Pico::FAgentToolCall& Call,
                            const Pico::FCancellationToken*)
    {
        ++HandlerCalls;
        return Pico::FAgentToolResult {Call.Id, true, "{}", {}, false};
    };
    Registry.Register(std::move(WriteTool));
    Approval.bApprove = true;
    const Pico::FAgentToolCall WriteCall {"write", "test.write", "{}"};
    Registry.PrepareApproval(WriteCall);
    const auto WriteResult = Registry.Execute(WriteCall, nullptr);
    Runner.Expect(
        WriteResult.bSucceeded && HandlerCalls == 1
            && Transaction.BeginCount == 0 && Transaction.CommitCount == 0,
        "Approved project writes use their own operation boundary without a World transaction");
    HandlerCalls = 0;

    Pico::FAgentToolPolicy DenyPolicy;
    DenyPolicy.ProjectRoot = Policy.ProjectRoot;
    Pico::FAgentToolRegistry DeniedRegistry(DenyPolicy, &Approval, &Transaction);
    Pico::FAgentToolDefinition PermissionTool;
    PermissionTool.Name = "test.denied";
    PermissionTool.Description = "Policy denied tool";
    PermissionTool.Permission = Pico::EAgentToolPermission::ModifyWorld;
    PermissionTool.Handler = [&](const Pico::FAgentToolCall& Call, const Pico::FCancellationToken*)
    {
        ++HandlerCalls;
        return Pico::FAgentToolResult {Call.Id, true, "{}", {}, false};
    };
    DeniedRegistry.Register(std::move(PermissionTool));
    const auto PolicyResult = DeniedRegistry.Execute({"policy", "test.denied", "{}"}, nullptr);
    Runner.Expect(
        !PolicyResult.bSucceeded && HandlerCalls == 0 && Transaction.BeginCount == 0,
        "Permission policy denial occurs before approval and transaction");
}

void TestCapabilityProviderRegistration(FTestRunner& Runner)
{
    class FProvider final : public Pico::IAgentCapabilityProvider
    {
    public:
        std::string Name = "TestCapabilityProvider";
        std::vector<Pico::FAgentToolDefinition> Definitions;
        std::string_view GetName() const override { return Name; }
        const std::vector<Pico::FAgentToolDefinition>& GetToolDefinitions() const override
        {
            return Definitions;
        }
        std::vector<Pico::FAgentKnowledgeRecord> CollectKnowledgeRecords() const override
        {
            Pico::FAgentKnowledgeRecord Record;
            Record.SourceType = "agent-capability";
            Record.SourcePath = Name;
            return {std::move(Record)};
        }
    };
    const auto MakeTool = [](std::string Name)
    {
        Pico::FAgentToolDefinition Tool;
        Tool.Name = std::move(Name);
        Tool.Description = "Capability provider test tool";
        Tool.RevisionReadSet = {"World.Revision"};
        Tool.RevisionWriteSet = {"World.Revision"};
        Tool.Handler = [](const Pico::FAgentToolCall& Call,
                          const Pico::FCancellationToken*)
        {
            return Pico::FAgentToolResult {Call.Id, true, "{}", {}, false};
        };
        return Tool;
    };

    Pico::FAgentToolRegistry Registry;
    FProvider Provider;
    Provider.Definitions.push_back(MakeTool("provider.first"));
    Provider.Definitions.push_back(MakeTool("provider.second"));
    const bool bRegistered = Registry.RegisterProvider(Provider);
    const std::string Catalog = Registry.BuildToolCatalogJson();
    Runner.Expect(bRegistered && Registry.GetToolNames().size() == 2
            && Catalog.find("TestCapabilityProvider") != std::string::npos
            && Catalog.find("revision_read_set") != std::string::npos,
        "Capability provider atomically publishes owned tools and revision metadata");
    const Pico::FAgentToolResult StructuredResult = Registry.Execute(
        {"provider-result", "provider.first", "{}"}, nullptr);
    Runner.Expect(StructuredResult.bSucceeded
            && StructuredResult.Status == Pico::EAgentToolResultStatus::Succeeded
            && StructuredResult.FactsJson == "{}"
            && StructuredResult.RevisionChanges.size() == 1
            && StructuredResult.RevisionChanges.front().Domain == "World.Revision",
        "Tool Registry normalizes legacy handler output into one structured result contract");

    FProvider Conflicting;
    Conflicting.Definitions.push_back(MakeTool("provider.temporary"));
    Conflicting.Definitions.push_back(MakeTool("provider.first"));
    Runner.Expect(!Registry.RegisterProvider(Conflicting)
            && !Registry.Contains("provider.temporary")
            && Registry.GetToolNames().size() == 2,
        "A provider registration conflict rolls back its earlier tool definitions");
}

void TestToolPipelineCommitAndRollback(FTestRunner& Runner)
{
    int Value = 0;
    FTestApproval Approval;
    FTestTransaction Transaction(Value);
    Pico::FAgentToolPolicy Policy;
    Policy.bAllowModifyWorld = true;
    Pico::FAgentToolRegistry Registry(Policy, &Approval, &Transaction);

    auto MakeTool = [&](std::string Name, bool bVerify)
    {
        Pico::FAgentToolDefinition Tool;
        Tool.Name = std::move(Name);
        Tool.Description = "Transactional mutation";
        Tool.Permission = Pico::EAgentToolPermission::ModifyWorld;
        Tool.Handler = [&](const Pico::FAgentToolCall& Call, const Pico::FCancellationToken*)
        {
            Value += 7;
            return Pico::FAgentToolResult {Call.Id, true, R"({"value":7})", {}, false};
        };
        Tool.Verifier = [bVerify](const Pico::FAgentToolCall&,
            const Pico::FAgentToolResult&, std::string& Error)
        {
            if (!bVerify) Error = "Injected postcondition failure";
            return bVerify;
        };
        return Tool;
    };
    Registry.Register(MakeTool("test.commit", true));
    Registry.Register(MakeTool("test.rollback", false));

    const Pico::FAgentToolCall CommitCall {"commit", "test.commit", "{}"};
    Registry.PrepareApproval(CommitCall);
    const auto CommitResult = Registry.Execute(CommitCall, nullptr);
    Runner.Expect(
        CommitResult.bSucceeded && Value == 7
            && Transaction.BeginCount == 1 && Transaction.CommitCount == 1,
        "Approved mutation executes, verifies, and commits exactly one transaction");

    const Pico::FAgentToolCall RollbackCall {"rollback", "test.rollback", "{}"};
    Registry.PrepareApproval(RollbackCall);
    const auto RollbackResult = Registry.Execute(RollbackCall, nullptr);
    Runner.Expect(
        !RollbackResult.bSucceeded && Value == 7
            && Transaction.RollbackCount == 1,
        "Failed postcondition rolls the mutation back to its exact prior value");

    const auto& Trace = Registry.GetLastTrace();
    Runner.Expect(
        !Trace.empty()
            && Trace.front().Stage == Pico::EAgentToolStage::Validate
            && Trace.back().Stage == Pico::EAgentToolStage::Transaction
            && Trace.back().bSucceeded,
        "Tool pipeline exposes ordered stage trace for later editor visualization");
}

void TestToolCallIdCollisionFailsClosed(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("id-collision");
    auto Session = Pico::FAgentSession::OpenOrCreate("id-collision", Path);
    Pico::FAgentProviderResponse First;
    First.ToolCalls.push_back({"same-id", "scene.fake", R"({"x":1})"});
    Pico::FAgentProviderResponse Collision;
    Collision.ToolCalls.push_back({"same-id", "scene.fake", R"({"x":2})"});
    Pico::FFakeAgentProvider Provider({{First, {}}, {Collision, {}}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const auto Result = Runtime.Run("reject an id collision");
    Runner.Expect(
        Result.Status == Pico::EAgentStatus::Failed
            && Result.Error.find("reused with different") != std::string::npos
            && Executor.Count == 1,
        "A stable ToolCall id cannot authorize changed arguments after its first execution");
}

void TestOpenAICompatibleProviderProtocolAndRetry(FTestRunner& Runner)
{
    auto Transport = std::make_shared<FScriptedHttpTransport>();
    Transport->Responses.push_back({true, 429,
        R"({"error":{"message":"rate limited"}})", 1, {}});
    Transport->Responses.push_back({true, 200,
        R"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"api-call-1","type":"function","function":{"name":"editor_actor_spawn","arguments":"{\"name\":\"AIBox\",\"kind\":\"Cube\"}"}}]}}],"usage":{"prompt_tokens":120,"completion_tokens":8,"prompt_cache_hit_tokens":90,"prompt_cache_miss_tokens":30}})",
        0, {}});

    Pico::FOpenAICompatibleProviderSettings Settings;
    Settings.Endpoint = "https://example.invalid/v1/chat/completions";
    Settings.Model = "test-model";
    Settings.ApiKey = "not-a-real-key";
    Settings.SystemPrompt = "Use Pico tools safely.";
    Settings.bSendThinkingSetting = true;
    Settings.bThinkingEnabled = false;
    Settings.InitialRetryDelayMilliseconds = 1;
    Settings.ToolCatalogJson = R"([{"name":"editor.actor.spawn","description":"Spawn","permission":"ModifyWorld","input_schema":{"type":"object","properties":{},"required":[],"additionalProperties":false}}])";
    Pico::FOpenAICompatibleProvider Provider(Settings, Transport);
    Pico::FAgentProviderRequest Request;
    Request.ProgressLedgerJson = R"({"goal":"Create a cube","state_revision":0})";
    Request.Messages.push_back({Pico::EAgentRole::User, "Create a cube"});
    const auto Result = Provider.Generate(Request, nullptr);
    Runner.Expect(
        Result.bSucceeded && !Result.bFinal && Result.ToolCalls.size() == 1
            && Result.ToolCalls[0].Name == "editor.actor.spawn"
            && Provider.GetRequestCount() == 2 && Provider.GetRetryCount() == 1
            && Result.Usage.bCacheDetailsAvailable
            && Result.Usage.CacheHitTokens == 90
            && Result.Usage.CacheMissTokens == 30,
        "OpenAI-compatible provider retries HTTP 429 and maps API-safe tool names back to Pico names");
    Runner.Expect(
        Transport->Bodies.size() == 2
            && Transport->Bodies[0].find("Authorization") == std::string::npos
            && Transport->Bodies[0].find("editor_actor_spawn") != std::string::npos
            && Transport->Bodies[0].find("Use Pico tools safely") != std::string::npos
            && Transport->Bodies[0].find("Pico harness progress ledger")
                != std::string::npos
            && Transport->Bodies[0].find("\"thinking\":{\"type\":\"disabled\"}")
                != std::string::npos,
        "Provider request contains schema, history, and explicit thinking mode but never serializes the API key into JSON");

    auto HistoryTransport = std::make_shared<FScriptedHttpTransport>();
    HistoryTransport->Responses.push_back({true, 200,
        R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"done"}}]})",
        0, {}});
    Pico::FOpenAICompatibleProvider HistoryProvider(Settings, HistoryTransport);
    Pico::FAgentProviderRequest HistoryRequest;
    Pico::FAgentMessage Assistant {Pico::EAgentRole::Assistant, ""};
    Assistant.ToolCalls.push_back({"api-call-1", "editor.actor.spawn", "{}"});
    HistoryRequest.Messages.push_back(std::move(Assistant));
    HistoryRequest.Messages.push_back(
        {Pico::EAgentRole::Tool, R"({"created":true})", "api-call-1"});
    const auto HistoryResult = HistoryProvider.Generate(HistoryRequest, nullptr);
    Runner.Expect(
        HistoryResult.bFinal && HistoryResult.Content == "done"
            && HistoryTransport->Bodies[0].find("tool_call_id") != std::string::npos
            && HistoryTransport->Bodies[0].find("api-call-1") != std::string::npos,
        "Provider reconstructs assistant ToolCall and matching tool result for multi-round chat");

    auto PrefixTransport = std::make_shared<FScriptedHttpTransport>();
    for (int Index = 0; Index < 2; ++Index)
        PrefixTransport->Responses.push_back({true, 200,
            R"({"choices":[{"finish_reason":"stop","message":{"content":"done"}}]})",
            0, {}});
    Pico::FOpenAICompatibleProvider PrefixProvider(Settings, PrefixTransport);
    Pico::FAgentProviderRequest PrefixRequest;
    PrefixRequest.SkillContextJson = R"([{"id":"asset-authoring"}])";
    PrefixRequest.KnowledgeContextJson = R"({"evidence":"stable"})";
    PrefixRequest.TaskStateJson = R"({"step":1})";
    PrefixRequest.Messages.push_back({Pico::EAgentRole::User, "inspect"});
    PrefixProvider.Generate(PrefixRequest, nullptr);
    PrefixRequest.TaskStateJson = R"({"step":2})";
    PrefixProvider.Generate(PrefixRequest, nullptr);
    const std::string& FirstBody = PrefixTransport->Bodies[0];
    const std::string& SecondBody = PrefixTransport->Bodies[1];
    const auto SkillPosition = FirstBody.find("Active Pico Skills");
    const auto KnowledgePosition = FirstBody.find("Pico Project Knowledge");
    const auto StatePosition = FirstBody.find("Pico task state");
    Runner.Expect(SkillPosition != std::string::npos
            && SkillPosition < KnowledgePosition && KnowledgePosition < StatePosition
            && FirstBody.substr(0, StatePosition)
                == SecondBody.substr(0, SecondBody.find("Pico task state"))
            && FirstBody != SecondBody,
        "Stable instructions, Skills, and knowledge precede mutable per-step task state");
}

void TestStreamingProviderAggregatesSse(FTestRunner& Runner)
{
    auto Transport = std::make_shared<FScriptedHttpTransport>();
    Transport->StreamChunks = {
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hel",
        "lo \"},\"finish_reason\":null}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"Pico\"},\"finish_reason\":\"stop\"}]}\n\n",
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":100,\"completion_tokens\":2,\"prompt_cache_hit_tokens\":75,\"prompt_cache_miss_tokens\":25}}\n\n",
        "data: [DONE]\n\n"
    };
    Pico::FOpenAICompatibleProviderSettings Settings;
    Settings.Endpoint = "https://example.invalid/chat/completions";
    Settings.Model = "stream-test";
    Settings.ApiKey = "not-a-real-key";
    Settings.ToolCatalogJson = "[]";
    Settings.bRequestStreamingUsage = true;
    Pico::FOpenAICompatibleProvider Provider(Settings, Transport);
    std::string Visible;
    Pico::FAgentProviderRequest Request;
    Request.Messages.push_back({Pico::EAgentRole::User, "stream"});
    Request.OnTextDelta = [&Visible](std::string_view Delta)
    {
        Visible.append(Delta);
    };
    const auto Result = Provider.Generate(Request, nullptr);
    Runner.Expect(
        Result.bSucceeded && Result.bFinal
            && Result.Content == "Hello Pico" && Visible == Result.Content
            && Result.Usage.bCacheDetailsAvailable
            && Result.Usage.CacheHitTokens == 75
            && Result.Usage.CacheMissTokens == 25
            && Transport->Bodies.size() == 1
            && Transport->Bodies[0].find("\"stream\":true") != std::string::npos
            && Transport->Bodies[0].find("\"include_usage\":true") != std::string::npos,
        "SSE chunks split inside JSON tokens stream visible text and persist one aggregate response");

    auto ToolTransport = std::make_shared<FScriptedHttpTransport>();
    ToolTransport->StreamChunks = {
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call-1\",\"function\":{\"name\":\"editor_actor_spawn\",\"arguments\":\"{\\\"name\\\":\"}}]},\"finish_reason\":null}]}\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"Box\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n",
        "data: [DONE]\n"
    };
    Settings.ToolCatalogJson = R"([{"name":"editor.actor.spawn","description":"Spawn","input_schema":{"type":"object","properties":{},"required":[]}}])";
    Pico::FOpenAICompatibleProvider ToolProvider(Settings, ToolTransport);
    Pico::FAgentProviderRequest ToolRequest;
    ToolRequest.Messages.push_back({Pico::EAgentRole::User, "spawn"});
    ToolRequest.OnTextDelta = [](std::string_view) {};
    const auto ToolResult = ToolProvider.Generate(ToolRequest, nullptr);
    Runner.Expect(
        ToolResult.bSucceeded && !ToolResult.bFinal
            && ToolResult.ToolCalls.size() == 1
            && ToolResult.ToolCalls[0].Name == "editor.actor.spawn"
            && ToolResult.ToolCalls[0].ArgumentsJson == R"({"name":"Box"})",
        "Streaming Tool Call fragments are fully aggregated before schema execution");
}

void TestKnowledgeStoreAndRagLite(FTestRunner& Runner)
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / "PicoAgentTests" / "Knowledge";
    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);
    Pico::FAgentKnowledgeStore Store(Root);
    std::string Error;
    Pico::FAgentKnowledgeRecord Movement;
    Movement.SourcePath = "Docs/Movement.md";
    Movement.Title = "Character movement";
    Movement.Content = "WASD controls the third person character movement component.";
    Movement.Tags = {"character", "movement"};
    Movement.SourceRevision = 3;
    Pico::FAgentKnowledgeRecord Packaging;
    Packaging.SourcePath = "Docs/Packaging.md";
    Packaging.Title = "Project packaging";
    Packaging.Content = "The packager creates a Windows stage.";
    const bool bSaved = Store.ReplaceSource(
        "project-file", {Movement, Packaging}, &Error);
    const auto Hits = Store.Query({"character movement", 4, 4096, {}});
    Pico::FAgentKnowledgeQuery HighConfidenceQuery;
    HighConfidenceQuery.Text = "character movement";
    HighConfidenceQuery.MaxResults = 4;
    HighConfidenceQuery.MaxContextBytes = 4096;
    const auto HighConfidenceResult = Store.QueryDetailed(HighConfidenceQuery);
    Pico::FAgentKnowledgeQuery PartialQuery;
    PartialQuery.Text = "character unrelated";
    PartialQuery.SourceTypes = {"project-file"};
    const auto PartialResult = Store.QueryDetailed(PartialQuery);
    Pico::FAgentKnowledgeQuery LegacyQuery = HighConfidenceQuery;
    LegacyQuery.bEnableBm25 = false;
    LegacyQuery.bEnableQueryRewrite = false;
    const auto LegacyResult = Store.QueryDetailed(LegacyQuery);
    const std::string Context = Store.BuildGroundingContextJson(
        {"character movement", 4, 4096, {}});
    std::size_t AuditLinesBefore = 0;
    { std::ifstream Audit(Store.GetAuditPath()); std::string Line;
      while (std::getline(Audit, Line)) ++AuditLinesBefore; }
    Store.ReplaceSource("project-file", {Movement, Packaging}, &Error);
    std::size_t AuditLinesAfter = 0;
    { std::ifstream Audit(Store.GetAuditPath()); std::string Line;
      while (std::getline(Audit, Line)) ++AuditLinesAfter; }
    Pico::FAgentKnowledgeStore Restored(Root);
    const bool bLoaded = Restored.Load(&Error);
    Runner.Expect(
        bSaved && bLoaded && Restored.GetRecordCount() == 2
            && !Hits.empty() && Hits.front().Record.Title == "Character movement"
            && Hits.front().Bm25Score > 0.0
            && !HighConfidenceResult.bRewriteApplied
            && HighConfidenceResult.EffectiveQueries.size() == 1
            && !PartialResult.Hits.empty()
            && PartialResult.Hits.front().ExactScore < 20.0
            && !LegacyResult.bRewriteApplied
            && LegacyResult.EffectiveQueries.size() == 1
            && Context.find("K:") != std::string::npos
            && Context.find("\"bm25_score\"") != std::string::npos
            && Context.find("\"rewrite_applied\":false") != std::string::npos
            && Context.find("Untrusted project evidence") != std::string::npos,
        "Project Knowledge Store distinguishes partial terms from exact identifiers, preserves legacy flags, and returns cited scored evidence without rewriting confident queries");
    Runner.Expect(
        AuditLinesBefore == 2 && AuditLinesAfter == AuditLinesBefore,
        "Knowledge audit is append-only for real changes and ignores identical refreshes");

    const std::filesystem::path Project = Root / "Project";
    std::filesystem::create_directories(Project / "Config");
    std::filesystem::create_directories(Project / "Saved/Agent");
    { std::ofstream(Project / "Config/Pico.ini") << "[Project]\nName=Safe\n"; }
    { std::ofstream(Project / "Saved/Agent/ApiKeys.ini") << "SECRET-MUST-NOT-INDEX"; }
    const auto ProjectRecords = Pico::CollectProjectTextKnowledge(Project);
    const bool bExcludedSavedSecret = std::none_of(
        ProjectRecords.begin(), ProjectRecords.end(), [](const auto& Record)
        {
            return Record.SourcePath.find("Saved") != std::string::npos
                || Record.Content.find("SECRET-MUST-NOT-INDEX") != std::string::npos;
        });
    Pico::FAgentKnowledgeRecord Large = Movement;
    Large.Id = "large-movement-guide";
    Large.Content.assign(9000, 'x');
    Large.Content += "\n\nrare_fragment_token describes the final movement section.";
    Store.ReplaceSource("large", {Large}, &Error);
    Pico::FAgentKnowledgeQuery ChunkQuery;
    ChunkQuery.Text = "rare_fragment_token";
    ChunkQuery.MaxResults = 4;
    ChunkQuery.MaxContextBytes = 1200;
    ChunkQuery.SourceTypes = {"large"};
    const Pico::FAgentKnowledgeQueryResult ChunkResult =
        Store.QueryDetailed(ChunkQuery);
    const std::string Bounded = Store.BuildGroundingContextJson(
        ChunkQuery);
    Runner.Expect(
        bExcludedSavedSecret && !ChunkResult.Hits.empty()
            && ChunkResult.Hits.front().Record.ParentId
                == "large-movement-guide"
            && ChunkResult.Hits.front().Record.ChunkCount > 1
            && ChunkResult.Hits.front().Record.Content.size() <= 4096
            && Bounded.size() < 2200
            && Bounded.find("rare_fragment_token") != std::string::npos,
        "Project retrieval excludes Saved secrets, chunks long documents, and bounds evidence");

    Pico::FAgentKnowledgeRecord CurrentDoor;
    CurrentDoor.Id = "door-current";
    CurrentDoor.SourcePath = "World.NetworkDoor";
    CurrentDoor.Title = "Current network door";
    CurrentDoor.Content = "Authoritative replicated door state";
    CurrentDoor.EntityIds = {"World.NetworkDoor"};
    CurrentDoor.RevisionDomain = "World.Revision";
    CurrentDoor.SourceRevision = 9;
    CurrentDoor.Fields = {{"replication", "authoritative"}};
    CurrentDoor.Kind = Pico::EAgentKnowledgeKind::Entity;
    Pico::FAgentKnowledgeRecord StaleDoor = CurrentDoor;
    StaleDoor.Id = "door-stale";
    StaleDoor.Title = "Stale network door";
    StaleDoor.SourceRevision = 8;
    Store.ReplaceSource("entity-test", {CurrentDoor, StaleDoor}, &Error);
    Pico::FAgentKnowledgeQuery EntityQuery;
    EntityQuery.Text = "door";
    EntityQuery.SourceTypes = {"entity-test"};
    EntityQuery.EntityIds = {"World.NetworkDoor"};
    EntityQuery.Revisions = {{"World.Revision", 9}};
    const auto EntityHits = Store.Query(EntityQuery);
    Pico::FAgentKnowledgeQuery LatestEntityQuery = EntityQuery;
    LatestEntityQuery.Revisions.clear();
    LatestEntityQuery.Kinds = {Pico::EAgentKnowledgeKind::Entity};
    const auto LatestEntityHits = Store.Query(LatestEntityQuery);
    Runner.Expect(EntityHits.size() == 1
            && EntityHits.front().Record.Id == "door-current"
            && EntityHits.front().EntityScore > 0.0
            && std::find(EntityHits.front().MatchedFields.begin(),
                EntityHits.front().MatchedFields.end(), "entity_id")
                != EntityHits.front().MatchedFields.end()
            && LatestEntityHits.size() == 1
            && LatestEntityHits.front().Record.Id == "door-current",
        "Entity and Revision views exclude stale records with explicit or derived latest revisions");

    Pico::FAgentKnowledgeRecord Gas;
    Gas.Id = "gas-rewrite";
    Gas.SourcePath = "Schema/GameplayAbilities.json";
    Gas.Title = "Gameplay ability loadout";
    Gas.Content = "gameplay ability system configures character abilities";
    Gas.Kind = Pico::EAgentKnowledgeKind::Procedure;
    Store.ReplaceSource("rewrite-test", {Gas}, &Error);
    Pico::FAgentKnowledgeQuery RewriteQuery;
    RewriteQuery.Text =
        "\xE8\xA7\x92\xE8\x89\xB2\xE6\x8A\x80\xE8\x83\xBD\xE7\xB3\xBB\xE7\xBB\x9F";
    RewriteQuery.SourceTypes = {"rewrite-test"};
    const Pico::FAgentKnowledgeQueryResult RewriteResult =
        Store.QueryDetailed(RewriteQuery);
    Runner.Expect(RewriteResult.bRewriteApplied
            && RewriteResult.EffectiveQueries.size() == 2
            && RewriteResult.OriginalQuery == RewriteQuery.Text
            && !RewriteResult.Hits.empty()
            && RewriteResult.Hits.front().Record.Id == "gas-rewrite"
            && RewriteResult.Hits.front().bFromRewrite,
        "Low-confidence deterministic Query Rewrite improves retrieval without changing the original query");

    Pico::FAgentKnowledgeRecord Episode;
    Episode.Id = "episode-session-1";
    Episode.SourcePath = "Sessions/editor-chat.jsonl";
    Episode.Title = "Recent conversation episode";
    Episode.Content = "The user configured the replicated network door.";
    Episode.Kind = Pico::EAgentKnowledgeKind::Episode;
    Episode.EntityIds = {"AgentSession:editor-chat"};
    Episode.RevisionDomain = "AgentSession.editor-chat";
    Episode.SourceRevision = 4;
    Store.ReplaceSource("session-episode", {Episode}, &Error);

    std::vector<Pico::FAgentKnowledgeRecord> CompressionRecords;
    for (int Index = 0; Index < 3; ++Index)
    {
        Pico::FAgentKnowledgeRecord Record;
        Record.Id = "compression-" + std::to_string(Index);
        Record.SourcePath = "Docs/Compression" + std::to_string(Index) + ".md";
        Record.Title = "Compression memory " + std::to_string(Index);
        Record.Content = "compression_token ";
        Record.Content.append(1800, static_cast<char>('a' + Index));
        CompressionRecords.push_back(std::move(Record));
    }
    Store.ReplaceSource("compression-test", std::move(CompressionRecords), &Error);
    Pico::FAgentKnowledgeQuery CompressionQuery;
    CompressionQuery.Text = "compression_token";
    CompressionQuery.SourceTypes = {"compression-test"};
    CompressionQuery.MaxResults = 3;
    CompressionQuery.MaxContextBytes = 12000;
    CompressionQuery.CompressionThresholdBytes = 2000;
    CompressionQuery.SummaryMaxBytes = 128;
    Pico::FAgentKnowledgeQueryResult CompressionResult;
    const std::string CompressedContext = Store.BuildGroundingContextJson(
        CompressionQuery, nullptr, &CompressionResult);
    Pico::FAgentKnowledgeQuery UncompressedQuery = CompressionQuery;
    UncompressedQuery.bEnableSummaryCompression = false;
    Pico::FAgentKnowledgeQueryResult UncompressedResult;
    Store.BuildGroundingContextJson(
        UncompressedQuery, nullptr, &UncompressedResult);
    Runner.Expect(CompressionResult.bCompressionApplied
            && CompressionResult.CompressedRecordCount > 0
            && CompressionResult.FinalEvidenceBytes
                < CompressionResult.OriginalEvidenceBytes
            && CompressedContext.find("\"content_mode\":\"summary\"")
                != std::string::npos
            && !UncompressedResult.bCompressionApplied
            && UncompressedResult.OriginalEvidenceBytes
                == UncompressedResult.FinalEvidenceBytes,
        "Knowledge summaries are deterministic, threshold-triggered, observable, and feature-flagged");

    Pico::FAgentKnowledgeStore MemoryRestored(Root);
    const bool bMemoryReloaded = MemoryRestored.Load(&Error);
    const auto EpisodeView = MemoryRestored.GetKindView(
        Pico::EAgentKnowledgeKind::Episode);
    const auto EntityView = MemoryRestored.GetKindView(
        Pico::EAgentKnowledgeKind::Entity);
    const Pico::FAgentKnowledgeViewStats ViewStats =
        MemoryRestored.GetViewStats();
    const std::string MemoryViews = MemoryRestored.BuildMemoryViewsJson();
    Runner.Expect(bMemoryReloaded && EpisodeView.size() == 1
            && EpisodeView.front().Id == "episode-session-1"
            && EntityView.size() == 1
            && EntityView.front().Id == "door-current"
            && ViewStats.ActiveByKind[static_cast<std::size_t>(
                Pico::EAgentKnowledgeKind::Episode)] == 1
            && ViewStats.StaleByKind[static_cast<std::size_t>(
                Pico::EAgentKnowledgeKind::Entity)] == 1
            && ViewStats.EntityCount == 1
            && MemoryViews.find("\"episode\"") != std::string::npos
            && MemoryViews.find("\"entity\"") != std::string::npos,
        "Episode and Entity memory are persistent derived views over one revision-aware Knowledge Store");
    std::filesystem::remove_all(Root, ErrorCode);
}

void TestPicoSkillRegistry(FTestRunner& Runner)
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / "PicoAgentTests" / "Skills";
    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);
    std::filesystem::create_directories(Root);
    {
        std::ofstream Skill(Root / "scene.pskill");
        Skill << R"SKILL({"format_version":1,"id":"scene-builder","version":"1.0.0","description":"Build scene","triggers":["room"],"allowed_tools":["editor.world.describe","editor.scene.create_room"],"preconditions":["World open"],"workflow":["Inspect","Create"],"completion_criteria":["Room exists"]})SKILL";
    }
    {
        std::ofstream Skill(Root / "play.pskill");
        Skill << R"SKILL({"format_version":1,"id":"validate-save-play","version":"1.0.0","description":"Play","triggers":["\u8fd0\u884c\u9879\u76ee"],"allowed_tools":["editor.world.describe"],"workflow":["Play"],"completion_criteria":["Started"]})SKILL";
    }
    {
        std::ofstream Skill(Root / "package.pskill");
        Skill << R"SKILL({"format_version":1,"id":"package-project","version":"1.0.0","description":"Package","triggers":["\u6253\u5305"],"allowed_tools":["editor.project.package"],"workflow":["Package"],"completion_criteria":["Packaged"]})SKILL";
    }
    Pico::FAgentSkillRegistry Registry;
    std::string Error;
    const bool bLoaded = Registry.LoadDirectory(Root,
        {"editor.world.describe", "editor.scene.create_room", "editor.project.package"},
        &Error);
    const auto Selected = Registry.Select("create a collision room");
    const std::string Catalog = Registry.PrioritizeToolCatalogJson(
        R"([{"name":"editor.world.describe"},{"name":"editor.scene.create_room"},{"name":"editor.project.package"}])",
        Selected);
    Runner.Expect(
        bLoaded && Selected.size() == 1
            && Registry.BuildSkillContextJson(Selected).find("completion_criteria")
                != std::string::npos
            && Registry.BuildSkillContextJson(Selected).find("recommended_tools")
                != std::string::npos
            && Catalog.find("editor.scene.create_room") != std::string::npos
            && Catalog.find("editor.project.package") != std::string::npos
            && Catalog.find("editor.scene.create_room")
                < Catalog.find("editor.project.package"),
        "Pico Skill selection is deterministic, prioritizes its tools, and preserves the full provider catalog");
    constexpr std::string_view PlayPrompt =
        "\xE8\xBF\x90\xE8\xA1\x8C\xE9\xA1\xB9\xE7\x9B\xAE"
        "\xE4\xBD\x86\xE4\xB8\x8D\xE8\xA6\x81\xE6\x89\x93\xE5\x8C\x85";
    constexpr std::string_view PackageTerm = "\xE6\x89\x93\xE5\x8C\x85";
    constexpr std::string_view PackagePrompt =
        "\xE6\x89\x93\xE5\x8C\x85\xE9\xA1\xB9\xE7\x9B\xAE"
        "\xE4\xBD\x86\xE4\xB8\x8D\xE8\xA6\x81\xE8\xBF\x90\xE8\xA1\x8C\xE9\xA1\xB9\xE7\x9B\xAE";
    constexpr std::string_view PlayTerm = "\xE8\xBF\x90\xE8\xA1\x8C";
    const auto PlayWithoutPackage = Registry.Select(PlayPrompt);
    Runner.Expect(
        PlayWithoutPackage.size() == 1
            && PlayWithoutPackage.front().Id == "validate-save-play"
            && !Pico::ContainsNonNegatedTerm(PlayPrompt, PackageTerm),
        "Negated packaging language selects Play without opening package tools");
    const auto PackageWithoutPlay = Registry.Select(PackagePrompt);
    Runner.Expect(
        PackageWithoutPlay.size() == 1
            && PackageWithoutPlay.front().Id == "package-project"
            && !Pico::ContainsNonNegatedTerm(PackagePrompt, PlayTerm),
        "Negated Play language selects Package without opening Play tools");
    std::filesystem::remove_all(Root, ErrorCode);
}

void TestIntentAndSkillEvalSet(FTestRunner& Runner)
{
    const std::vector<std::string> Tools = {
        "editor.world.describe", "editor.selection.describe", "editor.asset.search",
        "editor.asset.describe_catalog",
        "editor.object.describe", "editor.object.get_property",
        "editor.object.set_properties", "editor.object.batch_set_properties",
        "editor.component.list_types", "editor.component.add",
        "editor.component.remove",
        "editor.actor_blueprint.describe_defaults",
        "editor.actor_blueprint.set_defaults",
        "editor.actor.spawn", "editor.actor.spawn_blueprint", "editor.actor.delete",
        "editor.actor.delete_many", "editor.agent.list_changes",
        "editor.agent.revert_run",
        "editor.scene.create_room", "editor.gameplay.create_third_person_character",
        "editor.gameplay.asc.describe", "editor.gameplay.configure_ability_loadout",
        "editor.actor.set_location", "editor.play.validate", "editor.play.start",
        "editor.play.stop", "editor.world.save",
        "editor.project.create_from_third_person_template", "editor.project.package",
        "editor.graph.create", "editor.graph.describe", "editor.graph.add_node",
        "editor.graph.connect_pins", "editor.graph.set_default",
        "editor.graph.validate", "editor.graph.compile"
    };
    Pico::FAgentSkillRegistry Registry;
    std::string Error;
    const bool bLoaded = Registry.LoadDirectory("Config/Agent/Skills", Tools, &Error);
    Runner.Expect(bLoaded, "Agent eval loads the tracked production Skill manifests");
    if (!bLoaded) return;

    std::ifstream Input("Tests/Agent/Fixtures/IntentRoutingCases.tsv", std::ios::binary);
    Runner.Expect(static_cast<bool>(Input), "Agent intent and Skill routing eval fixture opens");
    std::string Line;
    std::size_t CaseIndex = 0;
    while (std::getline(Input, Line))
    {
        if (Line.empty() || Line.front() == '#') continue;
        const std::size_t First = Line.find('\t');
        const std::size_t Second = First == std::string::npos
            ? std::string::npos : Line.find('\t', First + 1);
        if (First == std::string::npos || Second == std::string::npos)
        {
            Runner.Expect(false, "Agent eval fixture row has three tab-separated fields");
            continue;
        }
        const std::string ExpectedIntent = Line.substr(0, First);
        const std::string ExpectedSkills = Line.substr(First + 1, Second - First - 1);
        const std::string Prompt = Line.substr(Second + 1);
        const std::vector<Pico::FAgentSkill> Selected = Registry.Select(Prompt);
        std::ostringstream ActualSkills;
        for (std::size_t Index = 0; Index < Selected.size(); ++Index)
        {
            if (Index > 0) ActualSkills << ',';
            ActualSkills << Selected[Index].Id;
        }
        ++CaseIndex;
        Runner.Expect(
            Pico::ToString(Pico::ClassifyAgentTurnIntent(Prompt)) == ExpectedIntent
                && ActualSkills.str() == ExpectedSkills,
            "Agent intent and Skill routing eval case " + std::to_string(CaseIndex));
    }
    Runner.Expect(CaseIndex >= 45, "Agent routing eval keeps at least 45 fixed prompts");
}

void TestGoldenTaskRunner(FTestRunner& Runner)
{
    std::vector<Pico::FAgentGoldenTask> Tasks;
    std::string Error;
    const bool bLoaded = Pico::FAgentGoldenTaskRunner::LoadTasks(
        "Tests/Agent/Fixtures/GoldenTasks.json", Tasks, &Error);
    Runner.Expect(bLoaded && Tasks.size() == 21,
        "Golden Task Runner loads twenty-one versioned functional and adversarial task definitions");
    if (!bLoaded) return;

    Pico::FAgentGoldenTaskHooks Hooks;
    Hooks.RuntimeContext.Features.bConditionalReflection = false;
    Hooks.Prepare = [](const Pico::FAgentGoldenTask& Task, std::string& OutError)
    {
        const bool bKnownFixture = Task.FixtureId == "empty-world"
            || Task.FixtureId == "starter-world"
            || Task.FixtureId == "playable-world"
            || Task.FixtureId == "selected-cube"
            || Task.FixtureId == "dirty-world";
        if (!bKnownFixture) OutError = "Unknown deterministic fixture";
        return bKnownFixture;
    };
    Hooks.CreateProvider = [](const Pico::FAgentGoldenTask& Task)
    {
        return CreateGoldenProvider(Task);
    };
    Hooks.CreateToolExecutor = [](const Pico::FAgentGoldenTask& Task)
    {
        return std::make_unique<FGoldenToolExecutor>(Task.Id);
    };
    Hooks.Verify = [](const Pico::FAgentGoldenTask& Task,
        const Pico::FAgentSession&, const Pico::FAgentRunResult&,
        const Pico::IAgentToolExecutor& Executor, std::string& OutError)
    {
        const auto* Golden = dynamic_cast<const FGoldenToolExecutor*>(&Executor);
        if (!Golden)
        {
            OutError = "Golden verifier received an incompatible executor";
            return false;
        }
        bool bVerified = false;
        if (Task.VerifierId == "cube-configured")
            bVerified = Golden->bCubeSpawned && Golden->bPropertiesChanged;
        else if (Task.VerifierId == "collision-room-exists")
            bVerified = Golden->bRoomCreated;
        else if (Task.VerifierId == "third-person-character-exists")
            bVerified = Golden->bCharacterCreated;
        else if (Task.VerifierId == "play-started-without-package")
            bVerified = Golden->bValidated && Golden->bSaved
                && Golden->bPlaying && !Golden->bPackaged;
        else if (Task.VerifierId == "package-created-without-play")
            bVerified = Golden->bValidated && Golden->bSaved
                && Golden->bPackaged && !Golden->bPlaying;
        else if (Task.VerifierId == "no-mutation-after-denial")
            bVerified = !Golden->bPropertiesChanged && !Golden->bSaved
                && !Golden->bPackaged;
        else if (Task.VerifierId == "single-save-side-effect")
        {
            const auto It = Golden->ExecutionCounts.find("editor.world.save");
            bVerified = Golden->bSaved && It != Golden->ExecutionCounts.end()
                && It->second == 1;
        }
        else if (Task.VerifierId == "property-modified-and-saved")
            bVerified = Golden->bPropertiesChanged && Golden->bSaved;
        else if (Task.VerifierId == "created-cubes-removed")
            bVerified = Golden->bBatchDeleted;
        else if (Task.VerifierId == "world-restored-to-before-run")
            bVerified = Golden->bRunReverted;
        else if (Task.VerifierId == "ability-loadout-configured")
            bVerified = Golden->bAscDescribed && Golden->bAbilityLoadoutConfigured;
        else if (Task.VerifierId == "graph-validated-and-compiled")
            bVerified = Golden->bGraphCreated && Golden->GraphNodesAdded == 1
                && Golden->bGraphDescribed && Golden->bGraphConnected && Golden->bGraphDefaultSet
                && Golden->bGraphValidated && Golden->bGraphCompiled;
        else if (Task.VerifierId == "prompt-injection-remained-data")
            bVerified = Golden->bWorldDescribed && !Golden->bPackaged
                && !Golden->bEngineWritten;
        else if (Task.VerifierId == "unknown-tool-had-no-side-effect")
            bVerified = !Golden->bPropertiesChanged && !Golden->bSaved
                && !Golden->bPackaged;
        else if (Task.VerifierId == "skill-forbidden-tool-had-no-side-effect")
            bVerified = !Golden->bPackaged;
        else if (Task.VerifierId == "tampered-arguments-conflicted")
        {
            const auto It = Golden->ExecutionCounts.find(
                "editor.object.set_properties");
            bVerified = Golden->bPropertiesChanged
                && It != Golden->ExecutionCounts.end() && It->second == 1;
        }
        else if (Task.VerifierId == "old-call-id-reused-once")
        {
            const auto It = Golden->ExecutionCounts.find("editor.world.save");
            bVerified = Golden->bSaved && It != Golden->ExecutionCounts.end()
                && It->second == 1;
        }
        else if (Task.VerifierId == "repeated-query-stopped")
        {
            const auto It = Golden->ExecutionCounts.find("editor.world.describe");
            bVerified = Golden->bWorldDescribed
                && It != Golden->ExecutionCounts.end() && It->second == 1;
        }
        else if (Task.VerifierId == "failed-mutation-had-no-side-effect")
            bVerified = !Golden->bPropertiesChanged && !Golden->bSaved;
        else if (Task.VerifierId == "play-package-intent-separated")
            bVerified = Golden->bValidated && Golden->bSaved
                && Golden->bPlaying && !Golden->bPackaged;
        else if (Task.VerifierId == "engine-scope-write-denied")
            bVerified = !Golden->bEngineWritten;
        if (!bVerified) OutError = "Deterministic scene/process postcondition failed";
        return bVerified;
    };

    const std::filesystem::path OutputRoot = std::filesystem::temp_directory_path()
        / "PicoAgentTests/GoldenTasks";
    std::error_code ErrorCode;
    std::filesystem::remove_all(OutputRoot, ErrorCode);
    const Pico::FAgentGoldenTaskRunner GoldenRunner;
    const std::vector<Pico::FAgentGoldenTaskResult> Results =
        GoldenRunner.Run(Tasks, OutputRoot, Hooks);
    const bool bAllPassed = Results.size() == Tasks.size()
        && std::all_of(Results.begin(), Results.end(),
            [](const Pico::FAgentGoldenTaskResult& Result)
            {
                return Result.bSucceeded && !Result.RunId.empty()
                    && std::filesystem::is_regular_file(Result.EventLogPath);
            });
    Runner.Expect(bAllPassed,
        "Golden Tasks verify functional paths plus prompt injection, permissions, replay, conflict, budget, and false-completion defenses");

    const std::filesystem::path ReportPath = OutputRoot / "GoldenTaskReport.json";
    Error.clear();
    const bool bReportWritten = Pico::FAgentGoldenTaskRunner::WriteReport(
        ReportPath, Results, &Error);
    std::ifstream ReportStream(ReportPath, std::ios::binary);
    const std::string ReportText((std::istreambuf_iterator<char>(ReportStream)), {});
    Runner.Expect(bReportWritten
            && ReportText.find("\"passed\": 21") != std::string::npos
            && ReportText.find("\"failed\": 0") != std::string::npos
            && ReportText.find("run_") != std::string::npos,
        "Golden Task report persists pass counts, metrics, RunIds, and event-log evidence");
}

void TestDeterministicRagBenchmark(FTestRunner& Runner)
{
    Pico::FAgentRagBenchmarkFixture Fixture;
    std::string Error;
    const bool bLoaded = Pico::FAgentRagBenchmarkRunner::LoadFixture(
        "Tests/Agent/Fixtures/RagBenchmark.json", Fixture, &Error);
    Runner.Expect(
        bLoaded && Fixture.Records.size() == 7 && Fixture.Cases.size() == 7,
        "RAG benchmark loads versioned records, expected evidence, and source policy");
    if (!bLoaded) return;

    const Pico::FAgentRagBenchmarkRunner Benchmark;
    const Pico::FAgentRagBenchmarkReport Report = Benchmark.Run(Fixture);
    Runner.Expect(
        Report.RecallAt1 == 1.0 && Report.RecallAt3 == 1.0
            && Report.RecallAt8 == 1.0
            && Report.MeanReciprocalRank == 1.0
            && Report.ForbiddenSourceRate == 0.0
            && Report.RecallAt3Gain > 0.0
            && Report.MeanReciprocalRankGain >= 0.0
            && Report.RewriteRate > 0.0
            && Report.MeanContextBytes > 0
            && Report.MeanRetrievalNanoseconds > 0,
        "R3 retrieval improves the fixed baseline while preserving perfect recall and source safety");

    const std::filesystem::path ReportPath =
        std::filesystem::temp_directory_path()
        / "PicoAgentTests/RagBenchmarkReport.json";
    const bool bWritten = Pico::FAgentRagBenchmarkRunner::WriteReport(
        ReportPath, Report, &Error);
    std::ifstream Stream(ReportPath, std::ios::binary);
    const std::string Text((std::istreambuf_iterator<char>(Stream)), {});
    Runner.Expect(
        bWritten && Text.find("\"recall_at_1\": 1.0") != std::string::npos
            && Text.find("\"forbidden_source_rate\": 0.0")
                != std::string::npos
            && Text.find("retrieval_nanoseconds") != std::string::npos
            && Text.find("recall_at_3_gain") != std::string::npos
            && Text.find("rewrite_rate") != std::string::npos,
        "RAG benchmark persists baseline gains, rewrite, recall, latency, and source metrics");
}

void TestCredentialStoreRejectsInvalidInput(FTestRunner& Runner)
{
    const Pico::FAgentCredentialStore Store(
        std::filesystem::temp_directory_path()
            / "PicoAgentTests/Credentials/ApiKeys.ini");
    std::string ApiKey;
    std::string Error;
    Runner.Expect(
        !Store.TryLoadApiKey("../DeepSeek", ApiKey, &Error)
            && !Error.empty() && ApiKey.empty(),
        "Credential store rejects provider ids that could escape its fixed namespace");
    Error.clear();
    Runner.Expect(
        !Store.SaveApiKey("DeepSeek", "short", &Error)
            && !Error.empty(),
        "Credential store rejects implausibly short API keys before local storage");
}

void TestStructuredToolResultAndArtifactPersistence(FTestRunner& Runner)
{
    const std::filesystem::path SessionPath = MakeLogPath("structured-result");
    std::error_code ErrorCode;
    std::filesystem::remove_all(SessionPath.parent_path() / "Artifacts", ErrorCode);
    auto Session = Pico::FAgentSession::OpenOrCreate(
        "structured-result", SessionPath);
    Pico::FAgentToolResult Result;
    Result.CallId = "large-result";
    Result.bSucceeded = true;
    Result.OutputJson = "{\"blob\":\"" + std::string(4096, 'x') + "\"}";
    Result.StateChanges = {"World actor created"};
    Result.RevisionChanges = {{"World.Revision", 4, 5}};
    Pico::NormalizeAgentToolResult(Result);

    std::string Error;
    const bool bExternalized = Session
        && Session->ExternalizeLargeToolResult(Result, 1024, &Error);
    Pico::FAgentEvent Event;
    Event.Type = Pico::EAgentEventType::ToolResult;
    Event.Role = Pico::EAgentRole::Tool;
    Event.CallId = Result.CallId;
    Event.ToolName = "test.large_result";
    Event.PayloadJson = Result.OutputJson;
    Event.StructuredResultJson = Pico::SerializeAgentToolResult(Result);
    Event.bSucceeded = Result.bSucceeded;
    const bool bAppended = bExternalized && Session
        && Session->Append(std::move(Event), &Error);

    auto Reopened = Pico::FAgentSession::OpenOrCreate(
        "structured-result", SessionPath, &Error);
    const auto Restored = Reopened
        ? Reopened->FindToolResult("large-result") : std::nullopt;
    const auto History = Reopened
        ? Reopened->BuildMessageHistory() : std::vector<Pico::FAgentMessage> {};
    const std::filesystem::path ArtifactPath = SessionPath.parent_path()
        / "Artifacts" / "large-result.facts.json";
    Runner.Expect(bAppended && Restored && Restored->bSucceeded
            && Restored->Status == Pico::EAgentToolResultStatus::Succeeded
            && Restored->Artifacts.size() == 1
            && Restored->StateChanges == std::vector<std::string> {"World actor created"}
            && Restored->RevisionChanges.size() == 1
            && std::filesystem::is_regular_file(ArtifactPath),
        "Structured Tool Result facts, state, revisions, and Artifact handles survive replay");
    Runner.Expect(!History.empty()
            && History.back().Content.find("artifact:large-result:facts")
                != std::string::npos
            && History.back().Content.find(std::string(256, 'x'))
                == std::string::npos,
        "Large Tool Results keep only a bounded Artifact handle and summary in model history");
}

void TestDurableOperationJournal(FTestRunner& Runner)
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / "PicoAgentTests/Operations";
    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);

    const Pico::FAgentToolCall Call {
        "../../stable-call", "editor.world.save", R"({"asset":"/Game/Main"})"};
    Pico::FAgentOperationJournal Journal(Root);
    std::string Error;
    const Pico::FAgentToolResult Applied {
        Call.Id, true, R"({"saved":true})", {}, false};
    Runner.Expect(
        Journal.Prepare(Call, &Error)
            && Journal.MarkExecuting(Call, &Error)
            && Journal.MarkApplied(Call, Applied, &Error),
        "Durable operation journal records Prepared, Executing, and Applied atomically");

    Pico::FAgentOperationJournal Restored(Root);
    const auto Recovered = Restored.FindApplied(Call, &Error);
    const auto Incomplete = Restored.ListIncomplete();
    Runner.Expect(
        Recovered && Recovered->bSucceeded && Recovered->bReused
            && Recovered->OutputJson.find("saved") != std::string::npos
            && Incomplete.size() == 1
            && Incomplete.front().State == Pico::EAgentOperationState::Applied,
        "Applied tool result survives restart and is reusable without repeating the side effect");

    const Pico::FAgentToolCall Changed {
        Call.Id, Call.Name, R"({"asset":"/Game/Other"})"};
    Runner.Expect(
        !Restored.FindApplied(Changed).has_value()
            && !Restored.Prepare(Changed, &Error) && !Error.empty(),
        "Operation id reuse with different arguments fails closed");
    Error.clear();
    std::size_t RecordFileCount = 0;
    for (const auto& Entry : std::filesystem::directory_iterator(Root))
        if (Entry.is_regular_file()) ++RecordFileCount;
    Runner.Expect(
        Restored.MarkCommitted(Call, &Error)
            && Restored.ListIncomplete().empty()
            && RecordFileCount == 1,
        "Committed operation leaves one hashed audit record and no path traversal output");
    std::filesystem::remove_all(Root, ErrorCode);
}

void TestDeterministicFailureInjectionAndReconcile(FTestRunner& Runner)
{
    std::size_t RecoverableCaseCount = 0;
    std::size_t RecoveredCaseCount = 0;
    {
        const auto Path = MakeLogPath("provider-failure-injection");
        auto Session = Pico::FAgentSession::OpenOrCreate(
            "provider-failure-injection", Path);
        Pico::FFakeAgentProvider Provider({
            {Final("discarded invalid response"), {}},
            {Final("provider recovered"), {}}});
        FCountingToolExecutor Executor;
        Pico::FAgentRuntimeContext Context;
        Context.FailureInjections = {
            Pico::EAgentFailureInjectionPoint::ProviderTimeout,
            Pico::EAgentFailureInjectionPoint::ProviderInvalidJson};
        Pico::FAgentRuntime Runtime(
            *Session, Provider, Executor, {}, std::move(Context));
        const Pico::FAgentRunResult Result = Runtime.Run(
            "exercise provider failure recovery");
        const bool bRecovered = Result.Status == Pico::EAgentStatus::Completed
                && Result.Counters.RepairAttempts == 2
                && Provider.GetGenerateCount() == 2;
        ++RecoverableCaseCount;
        RecoveredCaseCount += bRecovered ? 1 : 0;
        Runner.Expect(bRecovered,
            "Injected provider timeout and invalid JSON consume finite repair budget then recover");
    }

    {
        const auto Path = MakeLogPath("crash-before-execute");
        auto Session = Pico::FAgentSession::OpenOrCreate(
            "crash-before-execute", Path);
        Pico::FAgentProviderResponse ToolResponse;
        ToolResponse.ToolCalls.push_back(
            {"before-execute", "editor.actor.spawn", "{}"});
        Pico::FFakeAgentProvider Provider({{ToolResponse, {}}});
        FCountingToolExecutor Executor;
        Pico::FAgentRuntimeContext Context;
        Context.FailureInjections = {
            Pico::EAgentFailureInjectionPoint::CrashBeforeExecute};
        Pico::FAgentRuntime Runtime(
            *Session, Provider, Executor, {}, std::move(Context));
        const Pico::FAgentRunResult Result = Runtime.Run("fail before execute");
        Runner.Expect(Result.Status == Pico::EAgentStatus::Failed
                && Result.FailureClass == Pico::EAgentFailureClass::Infrastructure
                && Executor.Count == 0,
            "Crash-before-execute injection stops with zero side effects and preserves the session");
    }

    const std::array RecoveryPoints {
        Pico::EAgentFailureInjectionPoint::CrashAfterSideEffect,
        Pico::EAgentFailureInjectionPoint::CrashBeforePersist,
        Pico::EAgentFailureInjectionPoint::SessionAppendFailure};
    for (std::size_t Index = 0; Index < RecoveryPoints.size(); ++Index)
    {
        const std::string Name = "durable-reconcile-" + std::to_string(Index);
        const auto Path = MakeLogPath(Name);
        const auto JournalRoot = Path.parent_path() / (Name + "-operations");
        std::error_code ErrorCode;
        std::filesystem::remove_all(JournalRoot, ErrorCode);
        FDurableFailureExecutor Executor(JournalRoot);
        Pico::FAgentProviderResponse ToolResponse;
        ToolResponse.ToolCalls.push_back(
            {"stable-side-effect", "editor.actor.spawn", R"({"kind":"Cube"})"});

        auto FirstSession = Pico::FAgentSession::OpenOrCreate(Name, Path);
        Pico::FFakeAgentProvider FirstProvider({{ToolResponse, {}}});
        Pico::FAgentRuntimeContext Context;
        Context.FailureInjections = {RecoveryPoints[Index]};
        Pico::FAgentRuntime FirstRuntime(
            *FirstSession, FirstProvider, Executor, {}, std::move(Context));
        const Pico::FAgentRunResult First = FirstRuntime.Run("create once");

        auto Restored = Pico::FAgentSession::OpenOrCreate(Name, Path);
        Pico::FFakeAgentProvider RecoveryProvider(
            {{ToolResponse, {}}, {Final("reconciled"), {}}});
        Pico::FAgentRuntime RecoveryRuntime(
            *Restored, RecoveryProvider, Executor);
        const Pico::FAgentRunResult Recovered = RecoveryRuntime.Run("");
        const bool bRecovered = First.Status == Pico::EAgentStatus::Failed
                && Recovered.Status == Pico::EAgentStatus::Completed
                && Executor.SideEffectCount == 1
                && Executor.ReconcileCount == 1
                && Executor.Journal.ListIncomplete().empty();
        ++RecoverableCaseCount;
        RecoveredCaseCount += bRecovered ? 1 : 0;
        Runner.Expect(bRecovered,
            "Applied side effect is reconciled exactly once after injected persistence failure "
                + std::to_string(Index + 1));
        std::filesystem::remove_all(JournalRoot, ErrorCode);
    }

    const std::filesystem::path ReportPath =
        std::filesystem::temp_directory_path()
        / "PicoAgentTests/FailureInjectionReport.json";
    std::error_code ErrorCode;
    std::filesystem::create_directories(ReportPath.parent_path(), ErrorCode);
    std::ofstream Report(ReportPath, std::ios::binary | std::ios::trunc);
    const double RecoveryRate = RecoverableCaseCount == 0 ? 0.0
        : static_cast<double>(RecoveredCaseCount)
            / static_cast<double>(RecoverableCaseCount);
    Report << "{\n"
        << "  \"format_version\": 1,\n"
        << "  \"recoverable_failure_cases\": " << RecoverableCaseCount << ",\n"
        << "  \"recovered_failure_cases\": " << RecoveredCaseCount << ",\n"
        << "  \"recoverable_failure_recovery_rate\": "
        << RecoveryRate << "\n}\n";
    Runner.Expect(static_cast<bool>(Report),
        "Failure-injection evaluation persists its recovery-rate metric");
}

Pico::FAgentCapabilityCatalog BuildGameAssemblyCatalog(FTestRunner& Runner)
{
    Pico::FAgentCapabilityCatalog Catalog;
    const auto Add = [&Catalog, &Runner](
        std::string Id, std::vector<std::string> Tags,
        std::vector<std::string> RequiredKinds = {})
    {
        Pico::FAgentCapabilityDescriptor Descriptor;
        Descriptor.Id = std::move(Id);
        Descriptor.DisplayName = Descriptor.Id;
        Descriptor.Category = "GameAssembly";
        Descriptor.ProducerId = "test-producer";
        Descriptor.Tags = std::move(Tags);
        Descriptor.RequiredAssetKinds = std::move(RequiredKinds);
        Descriptor.SideEffect = "WriteProject";
        Descriptor.Approval = "PlanHash";
        Descriptor.VerifierId = "verify." + Descriptor.Id;
        Descriptor.Provenance = "PicoAgentTests fixture";
        std::string Error;
        Runner.Expect(Catalog.AddCapability(std::move(Descriptor), &Error),
            "Versioned game-assembly capability registers without ambiguity");
    };
    Add("player.control", {"player", "movement"});
    Add("network.coop", {"coop", "network"});
    Add("ability.different", {"ability", "coop"}, {"ActorBlueprint"});
    Add("collectible.pickup", {"collectible", "interaction"});
    Add("door.owned", {"door", "ownership", "interaction"});
    Add("mechanism.environment", {"mechanism", "interaction"});
    Add("match.shared-victory", {"objective", "victory", "authority"});
    Add("package.windows", {"package", "windows"});

    Pico::FAgentGameplayRecipe Recipe;
    Recipe.Id = "coop-owned-door";
    Recipe.DisplayName = "Cooperative owned door";
    Recipe.RequirementTags = {"door", "coop", "victory"};
    Recipe.CapabilityIds = {"network.coop", "door.owned", "match.shared-victory"};
    Recipe.ParametersSchemaJson = R"({"type":"object"})";
    Recipe.VerifierId = "verify.recipe.coop-owned-door";
    std::string Error;
    Runner.Expect(Catalog.AddRecipe(std::move(Recipe), &Error),
        "Gameplay Recipe references only registered capabilities");
    return Catalog;
}

void TestAssetDescriptorAndCapabilityCatalog(FTestRunner& Runner)
{
    Pico::FAgentAssetDescriptor Descriptor;
    Descriptor.Id = "asset:/Game/Characters/BP_Player.pblueprint";
    Descriptor.Kind = "ActorBlueprint";
    Descriptor.VirtualPath = "/Game/Characters/BP_Player.pblueprint";
    Descriptor.SourceRevision = 7;
    Descriptor.SizeBytes = 512;
    Descriptor.Dependencies = {"/Game/Characters/Player.pcharprofile"};
    Descriptor.Tags = {"asset", "ActorBlueprint", "player"};
    Descriptor.SummaryJson = R"({"component_count":4,"graph_count":1})";
    Descriptor.Provenance = "AssetRegistry fixture";
    Descriptor.ValidatorId = "asset.registry.record";
    std::string Error;
    Runner.Expect(Pico::ValidateAgentAssetDescriptor(Descriptor, &Error),
        "AssetDescriptor carries stable identity, structure, dependency, provenance, and verifier data");
    const std::string Serialized =
        Pico::SerializeAgentAssetDescriptors({Descriptor});
    Runner.Expect(Serialized.find("ActorBlueprint") != std::string::npos
            && Serialized.find("component_count") != std::string::npos,
        "AssetDescriptor serialization preserves typed summary evidence");

    Pico::FAgentCapabilityCatalog Catalog = BuildGameAssemblyCatalog(Runner);
    Pico::FAgentCapabilityDescriptor Duplicate;
    Duplicate.Id = "player.control";
    Duplicate.ProducerId = "test-producer";
    Duplicate.VerifierId = "verify.duplicate";
    Duplicate.Provenance = "fixture";
    Runner.Expect(!Catalog.AddCapability(std::move(Duplicate), &Error)
            && Error.find("Duplicate") != std::string::npos,
        "Capability Catalog rejects duplicate stable ids");
    Runner.Expect(Catalog.ToJson().find("coop-owned-door") != std::string::npos,
        "Capability Catalog publishes recipes and capability provenance as one contract");
}

void TestGameSpecRecipeAndSupportDiagnosis(FTestRunner& Runner)
{
    const std::string Prompt =
        "Build a two-player co-op game with different abilities, collect coins, "
        "an environment mechanism, an owned door, shared victory, and package for Windows.";
    std::string Error;
    const auto First = Pico::ParseAgentGameRequirement(Prompt, &Error);
    const auto Second = Pico::ParseAgentGameRequirement(Prompt, &Error);
    Runner.Expect(First && Second && First->Id == Second->Id
            && Pico::SerializeAgentGameSpec(*First)
                == Pico::SerializeAgentGameSpec(*Second)
            && First->PlayerCount == 2 && First->bNetworked
            && First->bPackageWindows,
        "Requirement Parser deterministically creates a versioned two-player PicoGameSpec");

    Pico::FAgentAssetDescriptor Blueprint;
    Blueprint.Id = "asset:/Game/BP_Player.pblueprint";
    Blueprint.Kind = "ActorBlueprint";
    Blueprint.VirtualPath = "/Game/BP_Player.pblueprint";
    Blueprint.SummaryJson = "{}";
    Blueprint.Provenance = "fixture";
    Blueprint.ValidatorId = "asset.registry.record";
    Pico::FAgentCapabilityCatalog Catalog = BuildGameAssemblyCatalog(Runner);
    const Pico::FAgentSupportDiagnosis Supported =
        Catalog.Diagnose(*First, {Blueprint});
    const Pico::FAgentSupportDiagnosis Missing = Catalog.Diagnose(*First, {});
    const bool bReportsMissingBlueprint = std::any_of(
        Missing.Requirements.begin(), Missing.Requirements.end(),
        [](const Pico::FAgentRequirementDiagnosis& Entry)
        {
            return Entry.Support
                    == Pico::EAgentRequirementSupport::MissingDependency
                && std::find(Entry.MissingAssetKinds.begin(),
                    Entry.MissingAssetKinds.end(), "ActorBlueprint")
                    != Entry.MissingAssetKinds.end();
        });
    Runner.Expect(Supported.bSupported && !Missing.bSupported
            && bReportsMissingBlueprint,
        "Support diagnosis distinguishes supported requirements from missing asset dependencies");
    Runner.Expect(Pico::SerializeAgentSupportDiagnosis(Missing)
            .find("MissingDependency") != std::string::npos,
        "Support diagnosis is machine-readable and explains why execution is blocked");

    Pico::FAgentCapabilityDescriptor AssetBackedAlternative;
    AssetBackedAlternative.Id = "ability.asset-backed";
    AssetBackedAlternative.ProducerId = "test-producer";
    AssetBackedAlternative.Tags = {"ability"};
    AssetBackedAlternative.RequiredAssetKinds = {"Texture"};
    AssetBackedAlternative.VerifierId = "verify.ability.asset-backed";
    AssetBackedAlternative.Provenance = "fixture";
    Pico::FAgentCapabilityDescriptor BuiltInAlternative = AssetBackedAlternative;
    BuiltInAlternative.Id = "ability.built-in";
    BuiltInAlternative.RequiredAssetKinds.clear();
    BuiltInAlternative.VerifierId = "verify.ability.built-in";
    Pico::FAgentCapabilityCatalog AlternativeCatalog;
    Runner.Expect(AlternativeCatalog.AddCapability(
            std::move(AssetBackedAlternative), &Error)
            && AlternativeCatalog.AddCapability(
                std::move(BuiltInAlternative), &Error),
        "Capability alternatives register for support diagnosis");
    Pico::FAgentGameSpec AlternativeSpec;
    AlternativeSpec.Id = "alternative-spec";
    AlternativeSpec.Requirements.push_back(
        {"ability-choice", {"ability"}, true, "Choose any available ability"});
    const Pico::FAgentSupportDiagnosis AlternativeDiagnosis =
        AlternativeCatalog.Diagnose(AlternativeSpec, {});
    Runner.Expect(AlternativeDiagnosis.bSupported
            && AlternativeDiagnosis.Requirements.size() == 1
            && AlternativeDiagnosis.Requirements.front().MissingAssetKinds.empty(),
        "One ready capability alternative satisfies a requirement without false missing dependencies");
}

void TestBuildPlanDagDryRunAndPlanHash(FTestRunner& Runner)
{
    const auto Spec = Pico::ParseAgentGameRequirement(
        "Build a two-player co-op game with different abilities, collect coins, "
        "an owned door, shared victory, and package for Windows.");
    Pico::FAgentAssetDescriptor Blueprint;
    Blueprint.Id = "asset:/Game/BP_Player.pblueprint";
    Blueprint.Kind = "ActorBlueprint";
    Blueprint.VirtualPath = "/Game/BP_Player.pblueprint";
    Blueprint.Provenance = "fixture";
    Blueprint.ValidatorId = "asset.registry.record";
    Pico::FAgentCapabilityCatalog Catalog = BuildGameAssemblyCatalog(Runner);
    const auto Diagnosis = Catalog.Diagnose(*Spec, {Blueprint});
    std::string Error;
    const auto Plan = Pico::BuildAgentBuildPlan(
        *Spec, Catalog, Diagnosis, &Error);
    const auto DryRun = Plan
        ? Pico::BuildAgentBuildPlanDryRun(*Plan, &Error) : std::nullopt;
    Runner.Expect(Plan && DryRun && !DryRun->OrderedStepIds.empty()
            && DryRun->PlanHash == Pico::HashAgentBuildPlan(*Plan)
            && DryRun->ReportJson.find("test-producer") != std::string::npos,
        "Build Plan emits a validated DAG and complete Dry Run bound to PlanHash");

    Pico::FAgentBuildPlan Changed = *Plan;
    Changed.Steps.front().ArgumentsJson = R"({"changed":true})";
    Runner.Expect(Pico::HashAgentBuildPlan(Changed) != DryRun->PlanHash,
        "Any approved plan content change invalidates PlanHash");

    Pico::FAgentBuildPlan Cyclic = *Plan;
    Cyclic.Steps.front().Dependencies = {Cyclic.Steps.back().Id};
    Runner.Expect(!Pico::ValidateAgentBuildPlan(Cyclic, nullptr, &Error)
            && Error.find("cycle") != std::string::npos,
        "Build Plan rejects dependency cycles before side effects");
}

void TestArtifactCheckpointAndIdempotentPlanResume(FTestRunner& Runner)
{
    class FProducer final : public Pico::IAgentArtifactProducer
    {
    public:
        std::string_view GetId() const override { return "test-producer"; }
        bool Produce(const Pico::FAgentBuildPlanStep& Step,
            const std::filesystem::path& StagingDirectory,
            std::vector<Pico::FAgentArtifact>& OutArtifacts,
            std::string& OutError) override
        {
            ++Calls;
            std::error_code Error;
            std::filesystem::create_directories(StagingDirectory, Error);
            if (Error) { OutError = Error.message(); return false; }
            OutArtifacts.push_back({"staged:" + Step.Id, "ProjectMutation",
                Step.Operation, StagingDirectory.generic_string(), 0});
            return true;
        }
        int Calls = 0;
    } Producer;
    class FTransaction final : public Pico::IAgentBuildPlanTransaction
    {
    public:
        bool Begin(std::string_view, std::string&) override
        { ++Begins; return true; }
        bool Commit(std::string&) override { ++Commits; return true; }
        bool Rollback(std::string&) override { ++Rollbacks; return true; }
        int Begins = 0;
        int Commits = 0;
        int Rollbacks = 0;
    } Transaction;

    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / "PicoAgentTests/GameAssembly";
    std::error_code Ignore;
    std::filesystem::remove_all(Root, Ignore);
    Pico::FAgentArtifactStore Artifacts(Root / "Artifacts");
    std::string Error;
    const auto Artifact = Artifacts.PublishText(
        "SupportDiagnosis", "Large structured evidence",
        std::string(80 * 1024, 'x'), &Error);
    const auto LoadedArtifact = Artifact
        ? Artifacts.ReadText(Artifact->Handle, &Error) : std::nullopt;
    Runner.Expect(Artifact && LoadedArtifact && LoadedArtifact->size() == 80 * 1024,
        "Artifact Store externalizes large evidence behind a content-addressed handle");

    Pico::FAgentBuildPlan Plan;
    Plan.Id = "resume-plan";
    Plan.GameSpecId = "resume-spec";
    Plan.Steps = {
        {"step-a", "test-producer", "create-a", "{}", {},
            {"ProjectMutation"}, "resume-spec:create-a", "WriteProject", "verify.a"},
        {"step-b", "test-producer", "create-b", "{}", {"step-a"},
            {"ProjectMutation"}, "resume-spec:create-b", "WriteProject", "verify.b"}};
    const std::string Hash = Pico::HashAgentBuildPlan(Plan);
    Pico::FAgentAssemblyCheckpointStore Checkpoints(Root / "checkpoint.json");
    const std::unordered_map<std::string, Pico::IAgentArtifactProducer*> Producers = {
        {"test-producer", &Producer}};
    const auto First = Pico::ExecuteAgentBuildPlan(Plan, Hash, Root / "Stage",
        Producers, Transaction, Checkpoints);
    const auto Restored = Checkpoints.Load(&Error);
    const int CallsAfterFirst = Producer.Calls;
    const auto Replayed = Restored
        ? Pico::ExecuteAgentBuildPlan(Plan, Hash, Root / "Stage",
            Producers, Transaction, Checkpoints, &*Restored)
        : Pico::FAgentBuildPlanExecutionResult{};
    Runner.Expect(First.bSucceeded && Restored && Replayed.bSucceeded
            && Replayed.bResumed && Producer.Calls == CallsAfterFirst
            && Restored->CompletedStepIds.size() == Plan.Steps.size(),
        "Checkpoint resume skips completed idempotency keys without repeating producer side effects");
    const auto Rejected = Pico::ExecuteAgentBuildPlan(Plan, "wrong-plan-hash",
        Root / "Stage", Producers, Transaction, Checkpoints);
    Runner.Expect(!Rejected.bSucceeded && Rejected.Error.find("PlanHash")
            != std::string::npos,
        "Build Plan execution rejects approval for a different PlanHash before transaction begin");
    std::filesystem::remove_all(Root, Ignore);
}
}

int main()
{
    FTestRunner Runner;
    TestDeterministicCompletionAndRecovery(Runner);
    TestFailureTaxonomyAndRecoveryPolicy(Runner);
    TestConditionalReflectionAndRecoveryEscalation(Runner);
    TestUnifiedTraceSpans(Runner);
    TestToolCallIdempotency(Runner);
    TestBoundedRepairAndBudget(Runner);
    TestSemanticReadCacheAndNoProgressGuard(Runner);
    TestReActTaskStateAndContextAssembler(Runner);
    TestReActObservationsEvidenceAndOscillation(Runner);
    TestCategorizedBudgetBeforeSideEffects(Runner);
    TestProjectHandoffIsOneShotAndCredentialFree(Runner);
    TestCheckpointResume(Runner);
    TestIncompleteTailRecoveryAndStateRules(Runner);
    TestCooperativeCancellation(Runner);
    TestToolPipelineRejectsBeforeSideEffects(Runner);
    TestCapabilityProviderRegistration(Runner);
    TestToolPipelineCommitAndRollback(Runner);
    TestToolCallIdCollisionFailsClosed(Runner);
    TestOpenAICompatibleProviderProtocolAndRetry(Runner);
    TestStreamingProviderAggregatesSse(Runner);
    TestKnowledgeStoreAndRagLite(Runner);
    TestPicoSkillRegistry(Runner);
    TestIntentAndSkillEvalSet(Runner);
    TestGoldenTaskRunner(Runner);
    TestDeterministicRagBenchmark(Runner);
    TestStructuredToolResultAndArtifactPersistence(Runner);
    TestCredentialStoreRejectsInvalidInput(Runner);
    TestDurableOperationJournal(Runner);
    TestDeterministicFailureInjectionAndReconcile(Runner);
    TestAssetDescriptorAndCapabilityCatalog(Runner);
    TestGameSpecRecipeAndSupportDiagnosis(Runner);
    TestBuildPlanDagDryRunAndPlanHash(Runner);
    TestArtifactCheckpointAndIdempotentPlanResume(Runner);
    return Runner.Finish();
}
