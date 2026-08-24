#pragma once

#include "Pico/Tasks/TaskSystem.h"

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace Pico
{
class FEditorSelection;
class FEditorTransactionManager;
class FEditorCommandService;
class FEditorWorldDocument;
class FEngineLoop;
class FGameThreadDispatcher;

class FAgentChatWorkspace
{
public:
    FAgentChatWorkspace(
        FEngineLoop* EngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        FTaskSystem* TaskSystem,
        FGameThreadDispatcher* Dispatcher,
        std::function<void()> OnWorldChanged,
        FEditorCommandService* Commands,
        FEditorWorldDocument* WorldDocument,
        std::function<std::pair<bool, std::string>(
            const std::filesystem::path&, const std::string&, bool)> StartPackage,
        std::function<std::pair<bool, std::string>()> StartPlay,
        std::function<std::pair<bool, std::string>()> StopPlay,
        std::function<void(const std::filesystem::path&)> RequestProjectOpen);
    ~FAgentChatWorkspace();

    FAgentChatWorkspace(const FAgentChatWorkspace&) = delete;
    FAgentChatWorkspace& operator=(const FAgentChatWorkspace&) = delete;

    void Draw(bool* Open);
    void Shutdown();

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
