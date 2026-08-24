#include "Pico/Agent/AgentProvider.h"

namespace Pico
{
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

std::string IAgentToolExecutor::GetLastExecutionTraceJson() const
{
    return "[]";
}
}
