#pragma once

#include "Pico/Agent/AgentToolRegistry.h"

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

struct FEditorAgentHostServices
{
    FEditorCommandService* Commands = nullptr;
    FEditorWorldDocument* WorldDocument = nullptr;
    std::function<std::pair<bool, std::string>(
        const std::filesystem::path&, const std::string&, bool)> StartPackage;
    std::function<std::pair<bool, std::string>()> StartPlay;
    std::function<std::pair<bool, std::string>()> StopPlay;
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
    const std::vector<FAgentToolStageTrace>& GetLastTrace() const;

    bool RequiresApproval(const FAgentToolCall& Call) const override;
    void PrepareApproval(const FAgentToolCall& Call) override;
    std::string GetLastExecutionTraceJson() const override;
    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) override;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
