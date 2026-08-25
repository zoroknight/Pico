#include "Pico/Agent/AgentEvaluation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

bool IsSafeTaskId(std::string_view Id)
{
    return !Id.empty() && Id.size() <= 96
        && std::all_of(Id.begin(), Id.end(), [](unsigned char Character)
        {
            return std::islower(Character) || std::isdigit(Character)
                || Character == '-';
        });
}

void AppendError(std::string& Error, std::string Message)
{
    if (!Error.empty()) Error += "; ";
    Error += std::move(Message);
}
}

bool FAgentGoldenTaskRunner::LoadTasks(
    const std::filesystem::path& Path,
    std::vector<FAgentGoldenTask>& OutTasks,
    std::string* OutError)
{
    OutTasks.clear();
    if (OutError) OutError->clear();
    try
    {
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream) throw std::runtime_error("Could not open Golden Task fixture");
        FJson Root;
        Stream >> Root;
        if (Root.value("format_version", 0) != 1 || !Root.contains("tasks")
            || !Root.at("tasks").is_array())
            throw std::runtime_error("Unsupported Golden Task fixture format");

        std::set<std::string> SeenIds;
        for (const FJson& Json : Root.at("tasks"))
        {
            FAgentGoldenTask Task;
            Task.Id = Json.value("id", "");
            Task.Prompt = Json.value("prompt", "");
            Task.FixtureId = Json.value("fixture", "");
            Task.VerifierId = Json.value("verifier", "");
            Task.RequiredTools = Json.value(
                "required_tools", std::vector<std::string> {});
            Task.ForbiddenTools = Json.value(
                "forbidden_tools", std::vector<std::string> {});
            Task.MaxToolCalls = Json.value("max_tool_calls", std::size_t {32});
            if (!TryParseAgentStatus(
                    Json.value("expected_status", "Completed"), Task.ExpectedStatus))
                throw std::runtime_error("Unknown expected status for " + Task.Id);
            if (!IsSafeTaskId(Task.Id) || Task.Prompt.empty()
                || Task.FixtureId.empty() || Task.VerifierId.empty()
                || Task.MaxToolCalls == 0 || !SeenIds.insert(Task.Id).second)
                throw std::runtime_error("Invalid Golden Task definition: " + Task.Id);
            OutTasks.push_back(std::move(Task));
        }
        if (OutTasks.empty())
            throw std::runtime_error("Golden Task fixture contains no tasks");
        return true;
    }
    catch (const std::exception& Exception)
    {
        OutTasks.clear();
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

std::vector<FAgentGoldenTaskResult> FAgentGoldenTaskRunner::Run(
    const std::vector<FAgentGoldenTask>& Tasks,
    const std::filesystem::path& OutputRoot,
    const FAgentGoldenTaskHooks& Hooks) const
{
    std::vector<FAgentGoldenTaskResult> Results;
    Results.reserve(Tasks.size());
    for (const FAgentGoldenTask& Task : Tasks)
    {
        FAgentGoldenTaskResult Evaluation;
        Evaluation.TaskId = Task.Id;
        const auto StartedAt = std::chrono::steady_clock::now();
        std::string Error;
        if (!IsSafeTaskId(Task.Id))
        {
            Evaluation.Error = "Golden Task id is not safe";
            Results.push_back(std::move(Evaluation));
            continue;
        }
        if (Hooks.Prepare && !Hooks.Prepare(Task, Error))
        {
            Evaluation.Error = "Fixture preparation failed: " + Error;
            Results.push_back(std::move(Evaluation));
            continue;
        }

        std::unique_ptr<IAgentProvider> Provider = Hooks.CreateProvider
            ? Hooks.CreateProvider(Task) : nullptr;
        std::unique_ptr<IAgentToolExecutor> Executor = Hooks.CreateToolExecutor
            ? Hooks.CreateToolExecutor(Task) : nullptr;
        if (!Provider || !Executor)
        {
            Evaluation.Error = "Golden Task environment did not create a provider and executor";
            Results.push_back(std::move(Evaluation));
            continue;
        }

        Evaluation.EventLogPath = OutputRoot / Task.Id / "Session.jsonl";
        std::error_code ErrorCode;
        std::filesystem::create_directories(
            Evaluation.EventLogPath.parent_path(), ErrorCode);
        std::filesystem::remove(Evaluation.EventLogPath, ErrorCode);
        auto Session = FAgentSession::OpenOrCreate(
            "golden-" + Task.Id, Evaluation.EventLogPath, &Error);
        if (!Session)
        {
            Evaluation.Error = "Could not create Golden Task session: " + Error;
            Results.push_back(std::move(Evaluation));
            continue;
        }

        FAgentBudget Budget;
        Budget.MaxToolCalls = Task.MaxToolCalls;
        Budget.MaxReadOnlyToolCalls = Task.MaxToolCalls;
        Budget.MaxMutationToolCalls = Task.MaxToolCalls;
        Budget.MaxSteps = std::max<std::size_t>(8, Task.MaxToolCalls + 2);
        FAgentRuntime Runtime(*Session, *Provider, *Executor, Budget);
        const FAgentRunResult RunResult = Runtime.Run(Task.Prompt);
        Evaluation.Status = RunResult.Status;
        Evaluation.RunId = RunResult.RunId;
        Evaluation.Counters = RunResult.Counters;
        Evaluation.Error = RunResult.Error;

        std::set<std::string> CalledTools;
        for (const FAgentEvent& Event : Session->GetEvents())
            if (Event.Type == EAgentEventType::ToolCall)
                CalledTools.insert(Event.ToolName);
        for (const std::string& Required : Task.RequiredTools)
            if (!CalledTools.contains(Required))
                AppendError(Evaluation.VerificationError,
                    "Required tool was not called: " + Required);
        for (const std::string& Forbidden : Task.ForbiddenTools)
            if (CalledTools.contains(Forbidden))
                AppendError(Evaluation.VerificationError,
                    "Forbidden tool was called: " + Forbidden);
        if (RunResult.Status != Task.ExpectedStatus)
            AppendError(Evaluation.VerificationError,
                "Expected status " + std::string(ToString(Task.ExpectedStatus))
                    + " but got " + std::string(ToString(RunResult.Status)));
        if (RunResult.Counters.ToolCalls > Task.MaxToolCalls)
            AppendError(Evaluation.VerificationError,
                "Tool-call count exceeded the Golden Task limit");
        if (Hooks.Verify)
        {
            std::string VerifyError;
            if (!Hooks.Verify(Task, *Session, RunResult, *Executor, VerifyError))
                AppendError(Evaluation.VerificationError,
                    VerifyError.empty() ? "Custom verifier failed" : VerifyError);
        }
        Evaluation.bSucceeded = Evaluation.VerificationError.empty();
        Evaluation.DurationMilliseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - StartedAt).count());
        Results.push_back(std::move(Evaluation));
    }
    return Results;
}

bool FAgentGoldenTaskRunner::WriteReport(
    const std::filesystem::path& Path,
    const std::vector<FAgentGoldenTaskResult>& Results,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    try
    {
        FJson Tasks = FJson::array();
        std::size_t Passed = 0;
        for (const FAgentGoldenTaskResult& Result : Results)
        {
            if (Result.bSucceeded) ++Passed;
            Tasks.push_back({{"id", Result.TaskId}, {"succeeded", Result.bSucceeded},
                {"status", ToString(Result.Status)}, {"run_id", Result.RunId},
                {"duration_ms", Result.DurationMilliseconds},
                {"tool_calls", Result.Counters.ToolCalls},
                {"read_only_tool_calls", Result.Counters.ReadOnlyToolCalls},
                {"mutation_tool_calls", Result.Counters.MutationToolCalls},
                {"repair_attempts", Result.Counters.RepairAttempts},
                {"error", Result.Error},
                {"verification_error", Result.VerificationError},
                {"event_log", Result.EventLogPath.generic_string()}});
        }
        const FJson Report = {{"format_version", 1}, {"passed", Passed},
            {"failed", Results.size() - Passed}, {"tasks", std::move(Tasks)}};
        std::filesystem::create_directories(Path.parent_path());
        const std::filesystem::path StagingPath = Path.string() + ".tmp";
        {
            std::ofstream Stream(StagingPath, std::ios::binary | std::ios::trunc);
            if (!Stream) throw std::runtime_error("Could not open Golden Task report staging file");
            Stream << Report.dump(2) << '\n';
            Stream.flush();
            if (!Stream) throw std::runtime_error("Could not flush Golden Task report");
        }
        std::error_code ErrorCode;
        std::filesystem::remove(Path, ErrorCode);
        std::filesystem::rename(StagingPath, Path);
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}
}
