#pragma once

#include "Pico/Agent/AgentIntent.h"
#include "Pico/Agent/AgentOperationJournal.h"
#include "Pico/Agent/AgentSkill.h"
#include "Pico/Agent/AgentProvider.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class FGameThreadDispatcher;

class FEditorAgentExecutionService final : public IAgentToolExecutor
{
public:
    using FAsyncCompletion = std::function<FAgentToolResult(
        const FAgentToolCall&,
        FAgentToolResult,
        const FCancellationToken*)>;

    FEditorAgentExecutionService(
        IAgentToolExecutor* EditorTools,
        FGameThreadDispatcher* Dispatcher,
        std::filesystem::path OperationDirectory,
        FAsyncCompletion AsyncCompletion = {});
    ~FEditorAgentExecutionService() override;

    FEditorAgentExecutionService(const FEditorAgentExecutionService&) = delete;
    FEditorAgentExecutionService& operator=(
        const FEditorAgentExecutionService&) = delete;

    void SetSessionId(std::string SessionId);
    void SetTurnIntent(EAgentTurnIntent Intent);
    void SetAllowedTools(const std::vector<FAgentSkill>& Skills);
    std::vector<FAgentOperationRecord> ListIncompleteOperations() const;
    void Shutdown();

    void BeginRun(std::string_view RunId) override;
    void EndRun(std::string_view RunId, EAgentStatus Status) override;
    bool RequiresApproval(const FAgentToolCall& Call) const override;
    bool IsReadOnly(const FAgentToolCall& Call) const override;
    std::vector<std::string> GetRevisionReadSet(
        const FAgentToolCall& Call) const override;
    std::vector<std::string> GetRevisionWriteSet(
        const FAgentToolCall& Call) const override;
    void PrepareApproval(const FAgentToolCall& Call) override;
    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) override;
    void CommitDurableResult(const FAgentToolCall& Call) override;
    std::string GetLastExecutionTraceJson() const override;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
