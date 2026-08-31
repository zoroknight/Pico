#pragma once

#include "Pico/Agent/AgentProvider.h"
#include "Pico/Mcp/McpServerCore.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class FMcpAgentToolsetAdapter final : public IMcpToolProvider
{
public:
    using FExecutionContextBinder =
        std::function<void(const FMcpToolCallContext&)>;

    FMcpAgentToolsetAdapter(
        IAgentToolExecutor& InExecutor,
        std::string ToolCatalogJson,
        std::string ToolsetVersion = "1.0.0",
        FExecutionContextBinder ExecutionContextBinder = {});
    ~FMcpAgentToolsetAdapter() override;

    FMcpAgentToolsetAdapter(const FMcpAgentToolsetAdapter&) = delete;
    FMcpAgentToolsetAdapter& operator=(
        const FMcpAgentToolsetAdapter&) = delete;

    bool IsValid() const;
    std::string GetError() const;
    std::vector<std::string> GetToolsetNames() const;
    bool SetToolsetEnabled(std::string_view Name, bool bEnabled);
    bool IsToolsetEnabled(std::string_view Name) const;

    std::vector<FMcpToolDescriptor> ListTools() const override;
    FMcpToolCallResult CallTool(
        const FMcpToolCallContext& Context,
        std::string_view Name,
        std::string_view ArgumentsJson,
        const FMcpCancellationToken& Cancellation) override;

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
