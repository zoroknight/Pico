#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
enum class EAgentRole
{
    System,
    User,
    Assistant,
    Tool
};

enum class EAgentStatus
{
    Idle,
    Planning,
    AwaitingApproval,
    ExecutingTool,
    Validating,
    Repairing,
    Completed,
    Failed,
    Cancelled
};

enum class EAgentEventType
{
    SessionCreated,
    StatusChanged,
    Message,
    ToolCall,
    ToolResult,
    Checkpoint,
    Error
};

struct FAgentToolCall
{
    std::string Id;
    std::string Name;
    std::string ArgumentsJson = "{}";
};

struct FAgentMessage
{
    EAgentRole Role = EAgentRole::User;
    std::string Content;
    std::string ToolCallId;
    std::vector<FAgentToolCall> ToolCalls;
};

struct FAgentToolResult
{
    std::string CallId;
    bool bSucceeded = false;
    std::string OutputJson = "{}";
    std::string Error;
    bool bReused = false;
};

struct FAgentBudget
{
    std::size_t MaxSteps = 16;
    std::size_t MaxToolCalls = 32;
    std::size_t MaxRepairAttempts = 2;
    std::uint64_t MaxElapsedMilliseconds = 30000;
};

struct FAgentCounters
{
    std::size_t Steps = 0;
    std::size_t ToolCalls = 0;
    std::size_t RepairAttempts = 0;
};

struct FAgentRunResult
{
    EAgentStatus Status = EAgentStatus::Idle;
    std::string FinalText;
    std::string Error;
    FAgentCounters Counters;
};

std::string_view ToString(EAgentRole Role);
std::string_view ToString(EAgentStatus Status);
std::string_view ToString(EAgentEventType Type);
bool TryParseAgentRole(std::string_view Text, EAgentRole& OutRole);
bool TryParseAgentStatus(std::string_view Text, EAgentStatus& OutStatus);
bool TryParseAgentEventType(std::string_view Text, EAgentEventType& OutType);
bool IsAllowedAgentTransition(EAgentStatus From, EAgentStatus To);
}
