#pragma once

#include "Pico/Tasks/TaskSystem.h"

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace Pico
{
class FEditorAgentHost;
class FGameThreadDispatcher;

class FAgentChatWorkspace
{
public:
    FAgentChatWorkspace(
        FEditorAgentHost* AgentHost,
        FTaskSystem* TaskSystem,
        FGameThreadDispatcher* Dispatcher,
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
