#pragma once

#include <memory>

namespace Pico
{
class FEditorAgentHost;

class FExternalAgentWorkspace
{
public:
    explicit FExternalAgentWorkspace(FEditorAgentHost* Host);
    ~FExternalAgentWorkspace();

    FExternalAgentWorkspace(const FExternalAgentWorkspace&) = delete;
    FExternalAgentWorkspace& operator=(const FExternalAgentWorkspace&) = delete;

    void Draw(bool* Open);
    void Shutdown();

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
