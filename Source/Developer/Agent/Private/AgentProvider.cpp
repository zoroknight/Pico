#include "Pico/Agent/AgentProvider.h"

namespace Pico
{
void IAgentToolExecutor::BeginRun(std::string_view)
{
}

void IAgentToolExecutor::EndRun(std::string_view, EAgentStatus)
{
}

bool IAgentToolExecutor::RequiresApproval(const FAgentToolCall&) const
{
    return false;
}

bool IAgentToolExecutor::IsReadOnly(const FAgentToolCall&) const
{
    return false;
}

void IAgentToolExecutor::PrepareApproval(const FAgentToolCall&)
{
}

void IAgentToolExecutor::CommitDurableResult(const FAgentToolCall&)
{
}

std::string IAgentToolExecutor::GetLastExecutionTraceJson() const
{
    return "[]";
}
}
