#pragma once

#include "Pico/Agent/AgentKnowledgeStore.h"
#include "Pico/Agent/AgentRuntime.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Pico
{
struct FAgentGoldenTask
{
    std::string Id;
    std::string Prompt;
    std::string FixtureId;
    std::string VerifierId;
    EAgentStatus ExpectedStatus = EAgentStatus::Completed;
    std::vector<std::string> RequiredTools;
    std::vector<std::string> ForbiddenTools;
    std::size_t MaxToolCalls = 32;
};

struct FAgentGoldenTaskResult
{
    std::string TaskId;
    bool bSucceeded = false;
    EAgentStatus Status = EAgentStatus::Idle;
    std::string RunId;
    std::string Error;
    std::string VerificationError;
    EAgentFailureClass FailureClass = EAgentFailureClass::None;
    EAgentRecoveryAction RecoveryAction = EAgentRecoveryAction::Abort;
    FAgentCounters Counters;
    std::uint64_t ContextBytes = 0;
    std::uint64_t DurationMilliseconds = 0;
    std::filesystem::path EventLogPath;
    std::filesystem::path MetricsPath;
};

struct FAgentGoldenTaskHooks
{
    FAgentRuntimeContext RuntimeContext;
    std::function<bool(const FAgentGoldenTask&, std::string&)> Prepare;
    std::function<std::unique_ptr<IAgentProvider>(const FAgentGoldenTask&)>
        CreateProvider;
    std::function<std::unique_ptr<IAgentToolExecutor>(const FAgentGoldenTask&)>
        CreateToolExecutor;
    std::function<bool(const FAgentGoldenTask&, const FAgentSession&,
        const FAgentRunResult&, const IAgentToolExecutor&, std::string&)>
        Verify;
};

class FAgentGoldenTaskRunner
{
public:
    static bool LoadTasks(
        const std::filesystem::path& Path,
        std::vector<FAgentGoldenTask>& OutTasks,
        std::string* OutError = nullptr);

    std::vector<FAgentGoldenTaskResult> Run(
        const std::vector<FAgentGoldenTask>& Tasks,
        const std::filesystem::path& OutputRoot,
        const FAgentGoldenTaskHooks& Hooks) const;

    static bool WriteReport(
        const std::filesystem::path& Path,
        const std::vector<FAgentGoldenTaskResult>& Results,
        std::string* OutError = nullptr);
};

struct FAgentRagBenchmarkCase
{
    std::string Id;
    std::string Query;
    std::vector<std::string> ExpectedRecordIds;
    std::vector<std::string> AllowedSourceTypes;
    std::vector<std::string> ForbiddenSourceTypes;
    std::vector<std::string> ExactIdentifiers;
    std::vector<std::string> EntityIds;
    std::unordered_map<std::string, std::uint64_t> Revisions;
};

struct FAgentRagBenchmarkFixture
{
    std::vector<FAgentKnowledgeRecord> Records;
    std::vector<FAgentRagBenchmarkCase> Cases;
};

struct FAgentRagBenchmarkCaseResult
{
    std::string Id;
    std::vector<std::string> RetrievedRecordIds;
    double RecallAt1 = 0.0;
    double RecallAt3 = 0.0;
    double RecallAt8 = 0.0;
    double ReciprocalRank = 0.0;
    std::size_t ContextBytes = 0;
    std::uint64_t RetrievalNanoseconds = 0;
    std::size_t ForbiddenSourceHits = 0;
    bool bRewriteApplied = false;
};

struct FAgentRagBenchmarkReport
{
    double RecallAt1 = 0.0;
    double RecallAt3 = 0.0;
    double RecallAt8 = 0.0;
    double MeanReciprocalRank = 0.0;
    double ForbiddenSourceRate = 0.0;
    double BaselineRecallAt3 = 0.0;
    double BaselineMeanReciprocalRank = 0.0;
    double RecallAt3Gain = 0.0;
    double MeanReciprocalRankGain = 0.0;
    double RewriteRate = 0.0;
    std::uint64_t MeanContextBytes = 0;
    std::uint64_t MeanRetrievalNanoseconds = 0;
    std::vector<FAgentRagBenchmarkCaseResult> Cases;
};

class FAgentRagBenchmarkRunner
{
public:
    static bool LoadFixture(
        const std::filesystem::path& Path,
        FAgentRagBenchmarkFixture& OutFixture,
        std::string* OutError = nullptr);
    FAgentRagBenchmarkReport Run(
        const FAgentRagBenchmarkFixture& Fixture) const;
    static bool WriteReport(
        const std::filesystem::path& Path,
        const FAgentRagBenchmarkReport& Report,
        std::string* OutError = nullptr);
};
}
