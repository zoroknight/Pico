#include "Pico/Agent/AgentMetrics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

void AddLatency(FAgentLatencyMetrics& Metrics, std::uint64_t DurationMicroseconds)
{
    ++Metrics.Count;
    Metrics.TotalMicroseconds += DurationMicroseconds;
    Metrics.MaxMicroseconds = std::max(
        Metrics.MaxMicroseconds, DurationMicroseconds);
}

FJson LatencyToJson(const FAgentLatencyMetrics& Metrics)
{
    return {{"count", Metrics.Count},
        {"total_us", Metrics.TotalMicroseconds},
        {"max_us", Metrics.MaxMicroseconds},
        {"average_us", Metrics.Count == 0 ? 0.0
            : static_cast<double>(Metrics.TotalMicroseconds)
                / static_cast<double>(Metrics.Count)}};
}

bool TraceRejectedByPolicy(std::string_view TraceJson)
{
    try
    {
        const FJson Trace = FJson::parse(TraceJson);
        if (!Trace.is_array()) return false;
        for (const FJson& Stage : Trace)
        {
            if (Stage.value("stage", "") == "Permission"
                && !Stage.value("succeeded", false)) return true;
        }
    }
    catch (...)
    {
    }
    return false;
}
}

std::string FAgentRunMetrics::ToJson() const
{
    return FJson {{"format_version", FormatVersion},
        {"run_id", RunId},
        {"status", ToString(Status)},
        {"counts", {{"runs", RunCount}, {"turns", TurnCount},
            {"tool_calls", ToolCallCount}, {"tool_results", ToolResultCount},
            {"repairs", RepairCount}, {"cache_hits", CacheHitCount},
            {"observations", ObservationCount},
            {"evidence_bindings", EvidenceBindingCount},
            {"oscillations", OscillationCount},
            {"reflections", ReflectionCount},
            {"recovery_escalations", RecoveryEscalationCount},
            {"forbidden_tools", ForbiddenToolCount}}},
        {"latency", {{"provider", LatencyToJson(ProviderLatency)},
            {"approval", LatencyToJson(ApprovalLatency)},
            {"tool", LatencyToJson(ToolLatency)},
            {"validation", LatencyToJson(ValidationLatency)}}},
        {"context_bytes", ContextBytes},
        {"provider_usage", {{"responses", ProviderUsageResponses},
            {"prompt_tokens", ProviderPromptTokens},
            {"completion_tokens", ProviderCompletionTokens},
            {"cache_detail_responses", ProviderCacheDetailResponses},
            {"cache_hit_tokens", ProviderCacheHitTokens},
            {"cache_miss_tokens", ProviderCacheMissTokens}}},
        {"context_assembly", {
            {"count", ContextMetrics.AssemblyCount},
            {"task_boundary_projection_enabled", bTaskBoundaryProjectionEnabled},
            {"total_us", ContextMetrics.TotalAssemblyMicroseconds},
            {"max_us", ContextMetrics.MaxAssemblyMicroseconds},
            {"instructions_bytes", ContextMetrics.InstructionBytes},
            {"task_state_bytes", ContextMetrics.TaskStateBytes},
            {"conversation_bytes", ContextMetrics.ConversationBytes},
            {"memory_bytes", ContextMetrics.MemoryBytes},
            {"observation_bytes", ContextMetrics.ObservationBytes},
            {"dropped_bytes", ContextMetrics.DroppedBytes},
            {"total_bytes", ContextMetrics.TotalBytes}}},
        {"history_projection", {
            {"old_messages", ContextMetrics.ProjectedMessages},
            {"saved_bytes", ContextMetrics.ProjectedHistoryBytes}}},
        {"tool_result_projection", {
            {"results", ContextMetrics.ProjectedToolResults},
            {"saved_bytes", ContextMetrics.ProjectedToolResultBytes}}},
        {"failure_class", ToString(FailureClass)},
        {"recovery_action", ToString(RecoveryAction)},
        {"automatically_retryable", bAutomaticallyRetryable},
        {"completion_rate", CompletionRate}}.dump(2);
}

bool FAgentRunMetrics::WriteJson(
    const std::filesystem::path& Path,
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    try
    {
        std::filesystem::create_directories(Path.parent_path());
        const std::filesystem::path StagingPath = Path.string() + ".tmp";
        {
            std::ofstream Stream(StagingPath, std::ios::binary | std::ios::trunc);
            if (!Stream) throw std::runtime_error("Could not open Agent Metrics staging file");
            Stream << ToJson() << '\n';
            Stream.flush();
            if (!Stream) throw std::runtime_error("Could not flush Agent Metrics report");
        }
        std::error_code ErrorCode;
        std::filesystem::remove(Path, ErrorCode);
        std::filesystem::rename(StagingPath, Path);
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

FAgentRunMetrics BuildAgentRunMetrics(
    const FAgentSession& Session,
    const FAgentRunResult& Result,
    std::uint64_t ContextBytes)
{
    FAgentRunMetrics Metrics;
    Metrics.RunId = Result.RunId;
    Metrics.Status = Result.Status;
    Metrics.ContextBytes = ContextBytes;
    Metrics.ContextMetrics = Result.ContextMetrics;
    Metrics.bTaskBoundaryProjectionEnabled =
        Result.bTaskBoundaryProjectionEnabled;
    Metrics.RepairCount = Result.Counters.RepairAttempts;
    Metrics.CacheHitCount = Result.Counters.SemanticCacheHits;
    Metrics.ProviderUsageResponses = Result.Counters.ProviderUsageResponses;
    Metrics.ProviderPromptTokens = Result.Counters.ProviderPromptTokens;
    Metrics.ProviderCompletionTokens = Result.Counters.ProviderCompletionTokens;
    Metrics.ProviderCacheDetailResponses = Result.Counters.ProviderCacheDetailResponses;
    Metrics.ProviderCacheHitTokens = Result.Counters.ProviderCacheHitTokens;
    Metrics.ProviderCacheMissTokens = Result.Counters.ProviderCacheMissTokens;
    Metrics.ObservationCount = Result.Counters.Observations;
    Metrics.EvidenceBindingCount = Result.Counters.EvidenceBindings;
    Metrics.OscillationCount = Result.Counters.OscillationsDetected;
    Metrics.ReflectionCount = Result.Counters.ReflectionAttempts;
    Metrics.RecoveryEscalationCount = Result.Counters.RecoveryEscalations;
    Metrics.CompletionRate = Result.Status == EAgentStatus::Completed ? 1.0 : 0.0;
    Metrics.FailureClass = Result.FailureClass;
    Metrics.RecoveryAction = Result.RecoveryAction;
    Metrics.bAutomaticallyRetryable =
        GetAgentRecoveryPolicy(Result.FailureClass).bAutomaticallyRetryable;

    for (const FAgentEvent& Event : Session.GetEvents())
    {
        if (Event.RunId != Result.RunId) continue;
        if (Event.Type == EAgentEventType::ToolCall) ++Metrics.ToolCallCount;
        if (Event.Type == EAgentEventType::ToolResult)
        {
            ++Metrics.ToolResultCount;
            if (TraceRejectedByPolicy(Event.TraceJson))
                ++Metrics.ForbiddenToolCount;
        }
        if (Event.Type != EAgentEventType::TraceSpan) continue;
        if (Event.SpanName == "AgentTurn") ++Metrics.TurnCount;
        else if (Event.SpanName == "Model.Generate")
            AddLatency(Metrics.ProviderLatency, Event.DurationMicroseconds);
        else if (Event.SpanName == "Tool.Approval")
            AddLatency(Metrics.ApprovalLatency, Event.DurationMicroseconds);
        else if (Event.SpanName == "Run.Validation")
            AddLatency(Metrics.ValidationLatency, Event.DurationMicroseconds);
        else if (Event.SpanName.starts_with("Tool."))
            AddLatency(Metrics.ToolLatency, Event.DurationMicroseconds);
    }
    return Metrics;
}
}
