#pragma once

#include "Pico/Agent/AgentTypes.h"

#include <filesystem>
#include <optional>
#include <string>
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
    std::string TraceJson = "[]";
    bool bSucceeded = false;
    bool bReused = false;
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
        std::string* OutError = nullptr);

    const std::string& GetId() const;
    const std::filesystem::path& GetEventLogPath() const;
    const std::vector<FAgentEvent>& GetEvents() const;
    EAgentStatus GetStatus() const;
    const FAgentCounters& GetCounters() const;
    std::vector<FAgentMessage> BuildMessageHistory() const;
    std::optional<FAgentToolCall> FindToolCall(std::string_view CallId) const;
    bool MatchesToolCall(const FAgentToolCall& Call) const;
    std::optional<FAgentToolResult> FindToolResult(std::string_view CallId) const;

private:
    bool Load(std::string* OutError);
    void Apply(const FAgentEvent& Event);

    std::string SessionId;
    std::filesystem::path EventLogPath;
    std::vector<FAgentEvent> Events;
    EAgentStatus Status = EAgentStatus::Idle;
    FAgentCounters Counters;
};
}
