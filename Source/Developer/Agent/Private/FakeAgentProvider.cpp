#include "Pico/Agent/FakeAgentProvider.h"

#include "Pico/Tasks/TaskSystem.h"

#include <algorithm>
#include <thread>

namespace Pico
{
FFakeAgentProvider::FFakeAgentProvider(std::vector<FFakeAgentStep> InSteps)
    : Steps(std::move(InSteps))
{
}

FAgentProviderResponse FFakeAgentProvider::Generate(
    const FAgentProviderRequest&,
    const FCancellationToken* CancellationToken)
{
    if (NextStep >= Steps.size())
    {
        return {false, false, {}, "Fake provider script exhausted", {}};
    }

    const FFakeAgentStep& Step = Steps[NextStep++];
    auto Remaining = Step.Delay;
    while (Remaining.count() > 0)
    {
        if (CancellationToken && CancellationToken->IsCancellationRequested())
        {
            return {false, false, {}, "Cancelled", {}};
        }
        const auto Slice = std::min(Remaining, std::chrono::milliseconds(5));
        std::this_thread::sleep_for(Slice);
        Remaining -= Slice;
    }
    return Step.Response;
}

std::size_t FFakeAgentProvider::GetGenerateCount() const
{
    return NextStep;
}
}
