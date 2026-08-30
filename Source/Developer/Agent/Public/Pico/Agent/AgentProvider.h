#pragma once

#include "Pico/Agent/AgentTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class FCancellationToken;

struct FAgentProviderRequest
{
    std::vector<FAgentMessage> Messages;
    std::string ProgressLedgerJson = "{}";
    std::string KnowledgeContextJson = "{}";
    std::string SkillContextJson = "[]";
    std::function<void(std::string_view)> OnTextDelta;
    std::size_t Step = 0;
    std::size_t RepairAttempt = 0;
};

struct FAgentProviderResponse
{
    bool bSucceeded = true;
    bool bFinal = false;
    std::string Content;
    std::string Error;
    std::vector<FAgentToolCall> ToolCalls;
    EAgentFailureClass FailureClass = EAgentFailureClass::None;
};

class IAgentProvider
{
public:
    virtual ~IAgentProvider() = default;
    virtual FAgentProviderResponse Generate(
        const FAgentProviderRequest& Request,
        const FCancellationToken* CancellationToken) = 0;
};

class IAgentToolExecutor
{
public:
    virtual ~IAgentToolExecutor() = default;
    virtual void BeginRun(std::string_view RunId);
    virtual void EndRun(std::string_view RunId, EAgentStatus Status);
    virtual bool RequiresApproval(const FAgentToolCall& Call) const;
    virtual bool IsReadOnly(const FAgentToolCall& Call) const;
    virtual std::vector<std::string> GetRevisionReadSet(
        const FAgentToolCall& Call) const;
    virtual std::vector<std::string> GetRevisionWriteSet(
        const FAgentToolCall& Call) const;
    virtual void PrepareApproval(const FAgentToolCall& Call);
    virtual void CommitDurableResult(const FAgentToolCall& Call);
    virtual std::string GetLastExecutionTraceJson() const;
    virtual FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) = 0;
};
}
