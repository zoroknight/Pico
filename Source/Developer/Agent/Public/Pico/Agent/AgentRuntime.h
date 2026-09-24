#pragma once

#include "Pico/Agent/AgentContext.h"
#include "Pico/Agent/AgentProvider.h"
#include "Pico/Agent/AgentSession.h"

#include <chrono>
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Pico
{
enum class EAgentFailureInjectionPoint
{
    ProviderTimeout,
    ProviderInvalidJson,
    CrashBeforeExecute,
    CrashAfterSideEffect,
    CrashBeforePersist,
    SessionAppendFailure
};

struct FAgentRuntimeContext
{
    std::string KnowledgeContextJson = "{}";
    std::string SkillContextJson = "[]";
    std::function<void(std::string_view)> OnAssistantDelta;
    std::vector<EAgentFailureInjectionPoint> FailureInjections;
    FAgentContextFeatureFlags Features;
};

class FAgentRuntime
{
public:
    FAgentRuntime(
        FAgentSession& Session,
        IAgentProvider& Provider,
        IAgentToolExecutor& ToolExecutor,
        FAgentBudget Budget = {},
        FAgentRuntimeContext Context = {});

    FAgentRunResult Run(
        std::string Prompt,
        const FCancellationToken* CancellationToken = nullptr);

private:
    struct FProgressAction
    {
        std::string ToolName;
        bool bReadOnly = false;
        bool bReused = false;
        std::vector<FAgentRevisionChange> RevisionChanges;
    };

    struct FRecentAction
    {
        std::string Fingerprint;
        bool bMadeProgress = false;
        std::uint64_t RevisionEpoch = 0;
    };

    struct FActiveSpan
    {
        std::string Id;
        std::string ParentId;
        std::string Name;
        std::string PayloadJson = "{}";
        std::int64_t StartedTimestampMilliseconds = 0;
        std::chrono::steady_clock::time_point StartedAt;
    };

    bool IsCancelled(const FCancellationToken* CancellationToken) const;
    bool CheckBudget(std::string& OutError) const;
    std::string MakeSemanticKey(const FAgentToolCall& Call) const;
    bool RecordActionAndDetectOscillation(
        const FAgentObservation& Observation);
    bool HasRequiredCompletionEvidence() const;
    bool TryEnterReflection(
        std::string Trigger,
        std::string Diagnosis,
        EAgentFailureClass FailureClass,
        std::string& OutError);
    std::string BuildProgressLedgerJson() const;
    std::string BuildTaskStateJson() const;
    bool WriteCheckpoint(EAgentStatus Status, std::string& OutError);
    void AccumulateContextMetrics(const FAgentContextMetrics& Metrics);
    FAgentToolResult MakeSemanticCacheResult(
        const FAgentToolCall& Call,
        const FAgentToolResult& Cached) const;
    bool Transition(EAgentStatus Status, std::string& OutError);
    FAgentRunResult Finish(
        EAgentStatus Status,
        std::string Error = {},
        EAgentFailureClass FailureClass = EAgentFailureClass::None);
    FActiveSpan BeginSpan(std::string Name, std::string ParentId);
    void EndSpan(FActiveSpan& Span, bool bSucceeded, std::string Error = {});
    void BeginTurn();
    void EndTurn(bool bSucceeded, std::string Error = {});
    bool ConsumeFailureInjection(EAgentFailureInjectionPoint Point);

    FAgentSession& Session;
    IAgentProvider& Provider;
    IAgentToolExecutor& ToolExecutor;
    FAgentBudget Budget;
    FAgentRuntimeContext Context;
    FAgentCounters Counters;
    FAgentTaskState TaskState;
    std::unordered_map<std::string, std::uint64_t> Revisions;
    std::unordered_map<std::string, FAgentToolResult> ReadOnlyCache;
    std::vector<FProgressAction> ProgressActions;
    std::vector<FAgentObservation> RecentObservations;
    std::vector<FRecentAction> RecentActions;
    std::uint64_t RevisionEpoch = 0;
    std::uint64_t ToolReplaySequenceFloor = 0;
    std::string ReflectionJson = "{}";
    bool bReflectionUsed = false;
    bool bObservedToolActivity = false;
    std::chrono::steady_clock::time_point StartTime;
    std::string RunId;
    std::uint64_t ContextBytes = 0;
    FAgentContextMetrics ContextMetrics;
    std::string TurnId;
    FActiveSpan RunSpan;
    FActiveSpan TurnSpan;
};
}
