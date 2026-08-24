#include "TestRunner.h"

#include "Pico/Agent/AgentRuntime.h"
#include "Pico/Agent/AgentCredentialStore.h"
#include "Pico/Agent/AgentToolRegistry.h"
#include "Pico/Agent/FakeAgentProvider.h"
#include "Pico/Agent/OpenAICompatibleProvider.h"
#include "Pico/Tasks/TaskSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

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
        return {Call.Id, true, R"({"changed":true})", {}, false};
    }

    int Count = 0;
    int PrepareApprovalCount = 0;
    bool bRequiresApproval = false;
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

    std::vector<Pico::FAgentHttpResponse> Responses;
    std::vector<std::string> Bodies;
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

void TestDeterministicCompletionAndRecovery(FTestRunner& Runner)
{
    const auto Path = MakeLogPath("completion");
    auto Session = Pico::FAgentSession::OpenOrCreate("completion", Path);
    Pico::FFakeAgentProvider Provider({{Final("done"), {}}});
    FCountingToolExecutor Executor;
    Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
    const Pico::FAgentRunResult Result = Runtime.Run("build a scene");
    Runner.Expect(
        Result.Status == Pico::EAgentStatus::Completed
            && Result.FinalText == "done" && Result.Counters.Steps == 1,
        "Fake provider drives a deterministic completed run");

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
            && HandlerCalls == 0 && Transaction.BeginCount == 0
            && Approval.RequestCount == 0 && SideEffect == 0,
        "Wrong types and project path traversal fail before approval, transaction, or handler");

    Approval.bApprove = false;
    const Pico::FAgentToolCall Denied {"denied", "test.modify", R"({"amount":5})"};
    Registry.PrepareApproval(Denied);
    const auto DeniedResult = Registry.Execute(Denied, nullptr);
    Runner.Expect(
        !DeniedResult.bSucceeded && HandlerCalls == 0
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
        R"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"api-call-1","type":"function","function":{"name":"editor_actor_spawn","arguments":"{\"name\":\"AIBox\",\"kind\":\"Cube\"}"}}]}}]})",
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
    Request.Messages.push_back({Pico::EAgentRole::User, "Create a cube"});
    const auto Result = Provider.Generate(Request, nullptr);
    Runner.Expect(
        Result.bSucceeded && !Result.bFinal && Result.ToolCalls.size() == 1
            && Result.ToolCalls[0].Name == "editor.actor.spawn"
            && Provider.GetRequestCount() == 2 && Provider.GetRetryCount() == 1,
        "OpenAI-compatible provider retries HTTP 429 and maps API-safe tool names back to Pico names");
    Runner.Expect(
        Transport->Bodies.size() == 2
            && Transport->Bodies[0].find("Authorization") == std::string::npos
            && Transport->Bodies[0].find("editor_actor_spawn") != std::string::npos
            && Transport->Bodies[0].find("Use Pico tools safely") != std::string::npos
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
}

void TestCredentialStoreRejectsInvalidInput(FTestRunner& Runner)
{
    std::string ApiKey;
    std::string Error;
    Runner.Expect(
        !Pico::FAgentCredentialStore::TryLoadApiKey("../DeepSeek", ApiKey, &Error)
            && !Error.empty() && ApiKey.empty(),
        "Credential store rejects provider ids that could escape its fixed namespace");
    Error.clear();
    Runner.Expect(
        !Pico::FAgentCredentialStore::SaveApiKey("DeepSeek", "short", &Error)
            && !Error.empty(),
        "Credential store rejects implausibly short API keys before project storage");
}
}

int main()
{
    FTestRunner Runner;
    TestDeterministicCompletionAndRecovery(Runner);
    TestToolCallIdempotency(Runner);
    TestBoundedRepairAndBudget(Runner);
    TestCheckpointResume(Runner);
    TestIncompleteTailRecoveryAndStateRules(Runner);
    TestCooperativeCancellation(Runner);
    TestToolPipelineRejectsBeforeSideEffects(Runner);
    TestToolPipelineCommitAndRollback(Runner);
    TestToolCallIdCollisionFailsClosed(Runner);
    TestOpenAICompatibleProviderProtocolAndRetry(Runner);
    TestCredentialStoreRejectsInvalidInput(Runner);
    return Runner.Finish();
}
