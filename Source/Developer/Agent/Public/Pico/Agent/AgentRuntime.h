#pragma once

#include "Pico/Agent/AgentProvider.h"
#include "Pico/Agent/AgentSession.h"

#include <chrono>

namespace Pico
{
class FAgentRuntime
{
public:
    FAgentRuntime(
        FAgentSession& Session,
        IAgentProvider& Provider,
        IAgentToolExecutor& ToolExecutor,
        FAgentBudget Budget = {});

    FAgentRunResult Run(
        std::string Prompt,
        const FCancellationToken* CancellationToken = nullptr);

private:
    bool IsCancelled(const FCancellationToken* CancellationToken) const;
    bool CheckBudget(std::string& OutError) const;
    bool Transition(EAgentStatus Status, std::string& OutError);
    FAgentRunResult Finish(EAgentStatus Status, std::string Error = {});

    FAgentSession& Session;
    IAgentProvider& Provider;
    IAgentToolExecutor& ToolExecutor;
    FAgentBudget Budget;
    FAgentCounters Counters;
    std::chrono::steady_clock::time_point StartTime;
};
}
