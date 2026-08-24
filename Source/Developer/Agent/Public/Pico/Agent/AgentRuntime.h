#pragma once

#include "Pico/Agent/AgentProvider.h"
#include "Pico/Agent/AgentSession.h"

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Pico
{
struct FAgentRuntimeContext
{
    std::string KnowledgeContextJson = "{}";
    std::string SkillContextJson = "[]";
    std::function<void(std::string_view)> OnAssistantDelta;
};

class FAgentRuntime
{
public:
    FAgentRuntime(
        FAgentSession& Session,
        IAgentProvider& Provider,
        IAgentToolExecutor& ToolExecutor,
        FAgentBudget Budget = {},
        FAgentRuntimeContext Context = {});

    FAgentRunResult Run(
        std::string Prompt,
        const FCancellationToken* CancellationToken = nullptr);

private:
    struct FProgressAction
    {
        std::string ToolName;
        bool bReadOnly = false;
        bool bReused = false;
        std::size_t StateRevision = 0;
    };

    bool IsCancelled(const FCancellationToken* CancellationToken) const;
    bool CheckBudget(std::string& OutError) const;
    std::string MakeSemanticKey(const FAgentToolCall& Call) const;
    std::string BuildProgressLedgerJson() const;
    FAgentToolResult MakeSemanticCacheResult(
        const FAgentToolCall& Call,
        const FAgentToolResult& Cached) const;
    bool Transition(EAgentStatus Status, std::string& OutError);
    FAgentRunResult Finish(EAgentStatus Status, std::string Error = {});

    FAgentSession& Session;
    IAgentProvider& Provider;
    IAgentToolExecutor& ToolExecutor;
    FAgentBudget Budget;
    FAgentRuntimeContext Context;
    FAgentCounters Counters;
    std::string CurrentGoal;
    std::size_t StateRevision = 0;
    std::unordered_map<std::string, FAgentToolResult> ReadOnlyCache;
    std::vector<FProgressAction> ProgressActions;
    std::chrono::steady_clock::time_point StartTime;
};
}
