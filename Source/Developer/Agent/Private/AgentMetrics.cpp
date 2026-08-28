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
            {"forbidden_tools", ForbiddenToolCount}}},
        {"latency", {{"provider", LatencyToJson(ProviderLatency)},
            {"approval", LatencyToJson(ApprovalLatency)},
            {"tool", LatencyToJson(ToolLatency)},
            {"validation", LatencyToJson(ValidationLatency)}}},
        {"context_bytes", ContextBytes},
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
    Metrics.RepairCount = Result.Counters.RepairAttempts;
    Metrics.CacheHitCount = Result.Counters.SemanticCacheHits;
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
