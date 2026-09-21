#include "Pico/Agent/AgentEvaluation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <map>
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
        Evaluation.FailureClass = RunResult.FailureClass;
        Evaluation.RecoveryAction = RunResult.RecoveryAction;
        Evaluation.MetricsPath = RunResult.MetricsPath;

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
                {"failure_class", ToString(Result.FailureClass)},
                {"recovery_action", ToString(Result.RecoveryAction)},
                {"error", Result.Error},
                {"verification_error", Result.VerificationError},
                {"event_log", Result.EventLogPath.generic_string()}});
            Tasks.back()["metrics"] = Result.MetricsPath.generic_string();
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

bool FAgentRagBenchmarkRunner::LoadFixture(
    const std::filesystem::path& Path,
    FAgentRagBenchmarkFixture& OutFixture,
    std::string* OutError)
{
    OutFixture = {};
    if (OutError) OutError->clear();
    try
    {
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream) throw std::runtime_error("Could not open RAG benchmark fixture");
        FJson Root;
        Stream >> Root;
        if (Root.value("format_version", 0) != 1
            || !Root.contains("records") || !Root.at("records").is_array()
            || !Root.contains("cases") || !Root.at("cases").is_array())
        {
            throw std::runtime_error("Unsupported RAG benchmark fixture format");
        }

        std::set<std::string> RecordIds;
        for (const FJson& Json : Root.at("records"))
        {
            FAgentKnowledgeRecord Record;
            Record.Id = Json.value("id", "");
            Record.SourceType = Json.value("source_type", "");
            Record.SourcePath = Json.value("source_path", "");
            Record.Title = Json.value("title", "");
            Record.Content = Json.value("content", "");
            Record.Tags = Json.value("tags", std::vector<std::string> {});
            Record.Provenance = Json.value("provenance", Record.SourcePath);
            Record.SourceRevision = Json.value("source_revision", 0ULL);
            Record.EntityIds = Json.value(
                "entity_ids", std::vector<std::string> {});
            Record.RevisionDomain = Json.value("revision_domain", "");
            Record.Fields = Json.value("fields",
                std::unordered_map<std::string, std::string> {});
            if (Record.Id.empty() || Record.SourceType.empty()
                || Record.Title.empty() || Record.Content.empty()
                || !RecordIds.insert(Record.Id).second)
            {
                throw std::runtime_error("Invalid RAG benchmark record");
            }
            OutFixture.Records.push_back(std::move(Record));
        }

        std::set<std::string> CaseIds;
        for (const FJson& Json : Root.at("cases"))
        {
            FAgentRagBenchmarkCase Case;
            Case.Id = Json.value("id", "");
            Case.Query = Json.value("query", "");
            Case.ExpectedRecordIds = Json.value(
                "expected_record_ids", std::vector<std::string> {});
            Case.AllowedSourceTypes = Json.value(
                "allowed_source_types", std::vector<std::string> {});
            Case.ForbiddenSourceTypes = Json.value(
                "forbidden_source_types", std::vector<std::string> {});
            Case.ExactIdentifiers = Json.value(
                "exact_identifiers", std::vector<std::string> {});
            Case.EntityIds = Json.value(
                "entity_ids", std::vector<std::string> {});
            Case.Revisions = Json.value("revisions",
                std::unordered_map<std::string, std::uint64_t> {});
            if (!IsSafeTaskId(Case.Id) || Case.Query.empty()
                || Case.ExpectedRecordIds.empty()
                || !CaseIds.insert(Case.Id).second)
            {
                throw std::runtime_error("Invalid RAG benchmark case: " + Case.Id);
            }
            for (const std::string& Expected : Case.ExpectedRecordIds)
                if (!RecordIds.contains(Expected))
                    throw std::runtime_error(
                        "Unknown expected RAG record: " + Expected);
            OutFixture.Cases.push_back(std::move(Case));
        }
        if (OutFixture.Records.empty() || OutFixture.Cases.empty())
            throw std::runtime_error("RAG benchmark fixture is empty");
        return true;
    }
    catch (const std::exception& Exception)
    {
        OutFixture = {};
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

FAgentRagBenchmarkReport FAgentRagBenchmarkRunner::Run(
    const FAgentRagBenchmarkFixture& Fixture) const
{
    FAgentRagBenchmarkReport Report;
    FAgentKnowledgeStore Store;
    std::map<std::string, std::vector<FAgentKnowledgeRecord>> Sources;
    for (const FAgentKnowledgeRecord& Record : Fixture.Records)
        Sources[Record.SourceType].push_back(Record);
    for (auto& [SourceType, Records] : Sources)
        Store.ReplaceSource(SourceType, std::move(Records));

    std::size_t TotalRetrieved = 0;
    std::size_t TotalForbidden = 0;
    std::size_t RewrittenCases = 0;
    for (const FAgentRagBenchmarkCase& Case : Fixture.Cases)
    {
        FAgentKnowledgeQuery Query;
        Query.Text = Case.Query;
        Query.MaxResults = 8;
        Query.MaxContextBytes = 12000;
        Query.SourceTypes = Case.AllowedSourceTypes;
        Query.ExactIdentifiers = Case.ExactIdentifiers;
        Query.EntityIds = Case.EntityIds;
        Query.Revisions = Case.Revisions;
        const auto StartedAt = std::chrono::steady_clock::now();
        const FAgentKnowledgeQueryResult QueryResult = Store.QueryDetailed(Query);
        const std::vector<FAgentKnowledgeHit>& Hits = QueryResult.Hits;
        const std::uint64_t RetrievalNanoseconds =
            static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - StartedAt).count());
        const std::string Context = Store.BuildGroundingContextJson(Query);

        FAgentRagBenchmarkCaseResult Result;
        Result.Id = Case.Id;
        Result.ContextBytes = Context.size();
        Result.RetrievalNanoseconds = RetrievalNanoseconds;
        Result.bRewriteApplied = QueryResult.bRewriteApplied;
        if (Result.bRewriteApplied) ++RewrittenCases;
        std::set<std::string> Expected(
            Case.ExpectedRecordIds.begin(), Case.ExpectedRecordIds.end());
        const auto RecallAt = [&](std::size_t K)
        {
            std::size_t Found = 0;
            for (std::size_t Index = 0;
                Index < std::min(K, Hits.size()); ++Index)
            {
                if (Expected.contains(Hits[Index].Record.Id)) ++Found;
            }
            return static_cast<double>(Found)
                / static_cast<double>(Expected.size());
        };
        Result.RecallAt1 = RecallAt(1);
        Result.RecallAt3 = RecallAt(3);
        Result.RecallAt8 = RecallAt(8);

        FAgentKnowledgeQuery BaselineQuery = Query;
        BaselineQuery.bEnableBm25 = false;
        BaselineQuery.bEnableQueryRewrite = false;
        BaselineQuery.ExactIdentifiers.clear();
        BaselineQuery.EntityIds.clear();
        BaselineQuery.Revisions.clear();
        const std::vector<FAgentKnowledgeHit> BaselineHits =
            Store.Query(BaselineQuery);
        std::size_t BaselineFoundAt3 = 0;
        double BaselineReciprocalRank = 0.0;
        for (std::size_t Index = 0; Index < BaselineHits.size(); ++Index)
        {
            if (!Expected.contains(BaselineHits[Index].Record.Id)) continue;
            if (Index < 3) ++BaselineFoundAt3;
            if (BaselineReciprocalRank == 0.0)
                BaselineReciprocalRank = 1.0
                    / static_cast<double>(Index + 1);
        }
        Report.BaselineRecallAt3 += static_cast<double>(BaselineFoundAt3)
            / static_cast<double>(Expected.size());
        Report.BaselineMeanReciprocalRank += BaselineReciprocalRank;
        for (std::size_t Index = 0; Index < Hits.size(); ++Index)
        {
            Result.RetrievedRecordIds.push_back(Hits[Index].Record.Id);
            if (Result.ReciprocalRank == 0.0
                && Expected.contains(Hits[Index].Record.Id))
            {
                Result.ReciprocalRank = 1.0
                    / static_cast<double>(Index + 1);
            }
            if (std::find(Case.ForbiddenSourceTypes.begin(),
                    Case.ForbiddenSourceTypes.end(),
                    Hits[Index].Record.SourceType)
                != Case.ForbiddenSourceTypes.end())
            {
                ++Result.ForbiddenSourceHits;
            }
        }
        TotalRetrieved += Hits.size();
        TotalForbidden += Result.ForbiddenSourceHits;
        Report.RecallAt1 += Result.RecallAt1;
        Report.RecallAt3 += Result.RecallAt3;
        Report.RecallAt8 += Result.RecallAt8;
        Report.MeanReciprocalRank += Result.ReciprocalRank;
        Report.MeanContextBytes += Result.ContextBytes;
        Report.MeanRetrievalNanoseconds += Result.RetrievalNanoseconds;
        Report.Cases.push_back(std::move(Result));
    }

    if (!Report.Cases.empty())
    {
        const double Count = static_cast<double>(Report.Cases.size());
        Report.RecallAt1 /= Count;
        Report.RecallAt3 /= Count;
        Report.RecallAt8 /= Count;
        Report.MeanReciprocalRank /= Count;
        Report.MeanContextBytes /= Report.Cases.size();
        Report.MeanRetrievalNanoseconds /= Report.Cases.size();
        Report.BaselineRecallAt3 /= Count;
        Report.BaselineMeanReciprocalRank /= Count;
    }
    Report.ForbiddenSourceRate = TotalRetrieved == 0 ? 0.0
        : static_cast<double>(TotalForbidden)
            / static_cast<double>(TotalRetrieved);
    Report.RecallAt3Gain = Report.RecallAt3 - Report.BaselineRecallAt3;
    Report.MeanReciprocalRankGain = Report.MeanReciprocalRank
        - Report.BaselineMeanReciprocalRank;
    Report.RewriteRate = Report.Cases.empty() ? 0.0
        : static_cast<double>(RewrittenCases)
            / static_cast<double>(Report.Cases.size());
    return Report;
}

bool FAgentRagBenchmarkRunner::WriteReport(
    const std::filesystem::path& Path,
    const FAgentRagBenchmarkReport& Report,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    try
    {
        FJson Cases = FJson::array();
        for (const FAgentRagBenchmarkCaseResult& Result : Report.Cases)
        {
            Cases.push_back({{"id", Result.Id},
                {"retrieved_record_ids", Result.RetrievedRecordIds},
                {"recall_at_1", Result.RecallAt1},
                {"recall_at_3", Result.RecallAt3},
                {"recall_at_8", Result.RecallAt8},
                {"reciprocal_rank", Result.ReciprocalRank},
                {"context_bytes", Result.ContextBytes},
                {"retrieval_nanoseconds", Result.RetrievalNanoseconds},
                {"forbidden_source_hits", Result.ForbiddenSourceHits},
                {"rewrite_applied", Result.bRewriteApplied}});
        }
        const FJson Json = {{"format_version", 1},
            {"recall_at_1", Report.RecallAt1},
            {"recall_at_3", Report.RecallAt3},
            {"recall_at_8", Report.RecallAt8},
            {"mrr", Report.MeanReciprocalRank},
            {"mean_context_bytes", Report.MeanContextBytes},
            {"mean_retrieval_nanoseconds", Report.MeanRetrievalNanoseconds},
            {"forbidden_source_rate", Report.ForbiddenSourceRate},
            {"baseline_recall_at_3", Report.BaselineRecallAt3},
            {"baseline_mrr", Report.BaselineMeanReciprocalRank},
            {"recall_at_3_gain", Report.RecallAt3Gain},
            {"mrr_gain", Report.MeanReciprocalRankGain},
            {"rewrite_rate", Report.RewriteRate},
            {"cases", std::move(Cases)}};
        std::filesystem::create_directories(Path.parent_path());
        const std::filesystem::path Staging = Path.string() + ".tmp";
        {
            std::ofstream Stream(Staging, std::ios::binary | std::ios::trunc);
            if (!Stream) throw std::runtime_error(
                "Could not open RAG benchmark report staging file");
            Stream << Json.dump(2) << '\n';
            Stream.flush();
            if (!Stream) throw std::runtime_error(
                "Could not flush RAG benchmark report");
        }
        std::error_code Error;
        std::filesystem::remove(Path, Error);
        std::filesystem::rename(Staging, Path, Error);
        if (Error) throw std::runtime_error(Error.message());
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}
}
