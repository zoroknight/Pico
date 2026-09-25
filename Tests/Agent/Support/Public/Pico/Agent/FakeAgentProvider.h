#pragma once

#include "Pico/Agent/AgentProvider.h"

#include <chrono>
#include <cstddef>
#include <vector>

namespace Pico
{
struct FFakeAgentStep
{
    FAgentProviderResponse Response;
    std::chrono::milliseconds Delay {0};
};

class FFakeAgentProvider final : public IAgentProvider
{
public:
    explicit FFakeAgentProvider(std::vector<FFakeAgentStep> Steps);

    FAgentProviderResponse Generate(
        const FAgentProviderRequest& Request,
        const FCancellationToken* CancellationToken) override;

    std::size_t GetGenerateCount() const;

private:
    std::vector<FFakeAgentStep> Steps;
    std::size_t NextStep = 0;
};
}
