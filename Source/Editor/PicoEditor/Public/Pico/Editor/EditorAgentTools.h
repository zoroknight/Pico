#pragma once

#include "Pico/Agent/AgentToolRegistry.h"
#include "Pico/Agent/AgentKnowledgeStore.h"
#include "Pico/Editor/EditorTransactionManager.h"

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
class FEditorSelection;
class FEditorTransactionManager;
class FEditorCommandService;
class FEditorWorldDocument;
class FEngineLoop;
class FCancellationToken;

struct FEditorAgentPackageCompletion
{
    bool bSucceeded = false;
    int ExitCode = -1;
    std::filesystem::path OutputDirectory;
    std::string Message;
};

struct FEditorAgentHostServices
{
    FEditorCommandService* Commands = nullptr;
    FEditorWorldDocument* WorldDocument = nullptr;
    std::function<std::pair<bool, std::string>(
        const std::filesystem::path&, const std::string&, bool)> StartPackage;
    std::function<FEditorAgentPackageCompletion(
        const FCancellationToken*)> WaitForPackage;
    std::function<std::pair<bool, std::string>()> StartPlay;
    std::function<std::pair<bool, std::string>()> StopPlay;
    FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot;
    std::filesystem::path ChangeSetDirectory;
};

class FEditorAgentToolExecutor final : public IAgentToolExecutor
{
public:
    FEditorAgentToolExecutor(
        FEngineLoop* EngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        IAgentToolApproval* Approval,
        std::function<void()> OnWorldChanged = {},
        FEditorAgentHostServices HostServices = {});
    ~FEditorAgentToolExecutor() override;

    bool IsInitialized() const;
    std::vector<std::string> GetToolNames() const;
    std::string BuildToolCatalogJson() const;
    std::vector<FAgentKnowledgeRecord> CollectKnowledgeRecords() const;
    const std::vector<FAgentToolStageTrace>& GetLastTrace() const;

    void BeginRun(std::string_view RunId) override;
    void EndRun(std::string_view RunId, EAgentStatus Status) override;
    bool RequiresApproval(const FAgentToolCall& Call) const override;
    bool IsReadOnly(const FAgentToolCall& Call) const override;
    std::vector<std::string> GetRevisionReadSet(
        const FAgentToolCall& Call) const override;
    std::vector<std::string> GetRevisionWriteSet(
        const FAgentToolCall& Call) const override;
    FAgentPendingReadback BuildPendingReadback(
        const FAgentToolCall& Call, const FAgentToolResult& Result) const override;
    bool ReadbackContainsTarget(
        const FAgentToolCall& ReadCall, const FAgentToolResult& ReadResult,
        const FAgentPendingReadback& Pending, std::string_view Target) const override;
    void PrepareApproval(const FAgentToolCall& Call) override;
    bool PrepareApprovalDecision(
        const FAgentToolCall& Call, bool bApproved) override;
    std::string GetLastExecutionTraceJson() const override;
    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) override;
    FAgentToolResult WaitForAsyncCompletion(
        const FAgentToolCall& Call,
        FAgentToolResult StartedResult,
        const FCancellationToken* CancellationToken) const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
