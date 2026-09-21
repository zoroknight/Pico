#pragma once

#include "Pico/Agent/AgentTypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Pico
{
struct FAgentEvent
{
    std::uint64_t Sequence = 0;
    std::int64_t TimestampMilliseconds = 0;
    EAgentEventType Type = EAgentEventType::Message;
    EAgentStatus Status = EAgentStatus::Idle;
    EAgentRole Role = EAgentRole::User;
    std::string Content;
    std::string CallId;
    std::string ToolName;
    std::string PayloadJson = "{}";
    std::string StructuredResultJson = "{}";
    std::string TraceJson = "[]";
    std::string RunId;
    std::string TurnId;
    std::string SpanId;
    std::string ParentSpanId;
    std::string SpanName;
    std::int64_t StartedTimestampMilliseconds = 0;
    std::uint64_t DurationMicroseconds = 0;
    bool bSucceeded = false;
    bool bReused = false;
    EAgentFailureClass FailureClass = EAgentFailureClass::None;
    EAgentRecoveryAction RecoveryAction = EAgentRecoveryAction::Abort;
    FAgentCounters Counters;
};

class FAgentSession
{
public:
    static std::optional<FAgentSession> OpenOrCreate(
        std::string SessionId,
        const std::filesystem::path& EventLogPath,
        std::string* OutError = nullptr);

    bool Append(FAgentEvent Event, std::string* OutError = nullptr);
    bool SetStatus(EAgentStatus Status, std::string* OutError = nullptr);
    bool WriteCheckpoint(
        EAgentStatus Status,
        const FAgentCounters& Counters,
        std::string* OutError = nullptr,
        std::string TaskStateJson = "{}");
    void SetTraceContext(
        std::string RunId,
        std::string TurnId = {},
        std::string SpanId = {});

    const std::string& GetId() const;
    const std::filesystem::path& GetEventLogPath() const;
    const std::vector<FAgentEvent>& GetEvents() const;
    EAgentStatus GetStatus() const;
    const FAgentCounters& GetCounters() const;
    std::string GetLatestTaskStateJson() const;
    std::vector<FAgentMessage> BuildMessageHistory() const;
    std::vector<FAgentMessage> BuildBoundedMessageHistory(
        std::size_t MaxMessages,
        std::uint64_t MaxBytes,
        std::size_t* OutTrimmedMessages = nullptr) const;
    std::string GetMostRecentError() const;
    std::unordered_map<std::string, std::uint64_t> BuildRevisionSnapshot() const;
    std::optional<FAgentToolCall> FindToolCall(
        std::string_view CallId,
        std::uint64_t MinSequence = 0) const;
    bool MatchesToolCall(
        const FAgentToolCall& Call,
        std::uint64_t MinSequence = 0) const;
    std::optional<FAgentToolResult> FindToolResult(
        std::string_view CallId,
        std::uint64_t MinSequence = 0) const;
    bool ExternalizeLargeToolResult(
        FAgentToolResult& Result,
        std::size_t ThresholdBytes = 64 * 1024,
        std::string* OutError = nullptr) const;

private:
    bool Load(std::string* OutError);
    void Apply(const FAgentEvent& Event);

    std::string SessionId;
    std::filesystem::path EventLogPath;
    std::vector<FAgentEvent> Events;
    EAgentStatus Status = EAgentStatus::Idle;
    FAgentCounters Counters;
    std::string CurrentRunId;
    std::string CurrentTurnId;
    std::string CurrentSpanId;
};
}
