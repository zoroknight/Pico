#pragma once

#include "Pico/Agent/AgentSession.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Pico
{
struct FAgentLatencyMetrics
{
    std::uint64_t Count = 0;
    std::uint64_t TotalMicroseconds = 0;
    std::uint64_t MaxMicroseconds = 0;
};

struct FAgentRunMetrics
{
    std::uint32_t FormatVersion = 1;
    std::string RunId;
    EAgentStatus Status = EAgentStatus::Idle;
    std::uint64_t RunCount = 1;
    std::uint64_t TurnCount = 0;
    std::uint64_t ToolCallCount = 0;
    std::uint64_t ToolResultCount = 0;
    std::uint64_t ContextBytes = 0;
    FAgentContextMetrics ContextMetrics;
    std::uint64_t RepairCount = 0;
    std::uint64_t CacheHitCount = 0;
    std::uint64_t ProviderUsageResponses = 0;
    std::uint64_t ProviderPromptTokens = 0;
    std::uint64_t ProviderCompletionTokens = 0;
    std::uint64_t ProviderCacheDetailResponses = 0;
    std::uint64_t ProviderCacheHitTokens = 0;
    std::uint64_t ProviderCacheMissTokens = 0;
    std::uint64_t ObservationCount = 0;
    std::uint64_t EvidenceBindingCount = 0;
    std::uint64_t OscillationCount = 0;
    std::uint64_t ReflectionCount = 0;
    std::uint64_t RecoveryEscalationCount = 0;
    std::uint64_t ForbiddenToolCount = 0;
    double CompletionRate = 0.0;
    EAgentFailureClass FailureClass = EAgentFailureClass::None;
    EAgentRecoveryAction RecoveryAction = EAgentRecoveryAction::Abort;
    bool bAutomaticallyRetryable = false;
    FAgentLatencyMetrics ProviderLatency;
    FAgentLatencyMetrics ApprovalLatency;
    FAgentLatencyMetrics ToolLatency;
    FAgentLatencyMetrics ValidationLatency;

    std::string ToJson() const;
    bool WriteJson(
        const std::filesystem::path& Path,
        std::string* OutError = nullptr) const;
};

FAgentRunMetrics BuildAgentRunMetrics(
    const FAgentSession& Session,
    const FAgentRunResult& Result,
    std::uint64_t ContextBytes);
}
