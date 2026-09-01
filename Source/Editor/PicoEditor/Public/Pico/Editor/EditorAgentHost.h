#pragma once

#include "Pico/Agent/AgentIntent.h"
#include "Pico/Agent/AgentSkill.h"
#include "Pico/Editor/EditorAgentTools.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Pico
{
class FEditorAgentExecutionService;
class FEditorSelection;
class FEditorTransactionManager;
class FEditorCommandService;
class FEditorWorldDocument;
class FEngineLoop;
class FGameThreadDispatcher;

class FEditorAgentHost
{
public:
    FEditorAgentHost(
        FEngineLoop* EngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        FGameThreadDispatcher* Dispatcher,
        std::function<void()> OnWorldChanged,
        FEditorAgentHostServices HostServices);
    FEditorAgentHost(
        FEngineLoop* EngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        FGameThreadDispatcher* Dispatcher,
        std::function<void()> OnWorldChanged,
        FEditorCommandService* Commands,
        FEditorWorldDocument* WorldDocument,
        std::function<std::pair<bool, std::string>(
            const std::filesystem::path&, const std::string&, bool)> StartPackage,
        std::function<FEditorAgentPackageCompletion(
            const FCancellationToken*)> WaitForPackage,
        std::function<std::pair<bool, std::string>()> StartPlay,
        std::function<std::pair<bool, std::string>()> StopPlay,
        FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot);
    ~FEditorAgentHost();

    FEditorAgentHost(const FEditorAgentHost&) = delete;
    FEditorAgentHost& operator=(const FEditorAgentHost&) = delete;

    FEditorAgentToolExecutor& GetTools();
    FEditorAgentExecutionService& GetExecutionService();
    void ConfigureSession(
        std::string SessionId,
        EAgentTurnIntent Intent,
        const std::vector<FAgentSkill>& Skills = {});
    void CancelPendingApproval();
    void DrawApprovalCenter();
    void Shutdown();

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
