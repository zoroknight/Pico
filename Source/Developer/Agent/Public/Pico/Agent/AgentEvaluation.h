#pragma once

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
    FAgentCounters Counters;
    std::uint64_t DurationMilliseconds = 0;
    std::filesystem::path EventLogPath;
};

struct FAgentGoldenTaskHooks
{
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
}
