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

std::vector<std::string> IAgentToolExecutor::GetRevisionReadSet(
    const FAgentToolCall&) const
{
    return {"State.Revision"};
}

std::vector<std::string> IAgentToolExecutor::GetRevisionWriteSet(
    const FAgentToolCall&) const
{
    return {"State.Revision"};
}

FAgentPendingReadback IAgentToolExecutor::BuildPendingReadback(
    const FAgentToolCall& Call, const FAgentToolResult&) const
{
    FAgentPendingReadback Pending;
    Pending.ToolName = Call.Name;
    return Pending;
}

bool IAgentToolExecutor::ReadbackContainsTarget(
    const FAgentToolCall&, const FAgentToolResult&,
    const FAgentPendingReadback&, std::string_view) const
{
    return false;
}

void IAgentToolExecutor::PrepareApproval(const FAgentToolCall&)
{
}

bool IAgentToolExecutor::PrepareApprovalDecision(const FAgentToolCall&, bool)
{
    return false;
}

void IAgentToolExecutor::CommitDurableResult(const FAgentToolCall&)
{
}

std::string IAgentToolExecutor::GetLastExecutionTraceJson() const
{
    return "[]";
}
}
