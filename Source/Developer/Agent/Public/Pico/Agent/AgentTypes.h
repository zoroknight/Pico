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

enum class EAgentFailureClass
{
    None,
    ModelProtocol,
    InvalidArguments,
    PermissionDenied,
    ApprovalRejected,
    PreconditionFailed,
    ExecutionFailed,
    VerificationFailed,
    Infrastructure,
    BudgetExceeded,
    Conflict,
    Cancelled
};

enum class EAgentRecoveryAction
{
    Retry,
    RefreshState,
    Replan,
    WaitForApproval,
    AskUser,
    Rollback,
    Abort
};

struct FAgentRecoveryPolicy
{
    EAgentRecoveryAction Action = EAgentRecoveryAction::Abort;
    bool bAutomaticallyRetryable = false;
};

enum class EAgentEventType
{
    SessionCreated,
    StatusChanged,
    Message,
    ToolCall,
    ToolResult,
    TraceSpan,
    Checkpoint,
    Error
};

struct FAgentToolCall
{
    std::string Id;
    std::string Name;
    std::string ArgumentsJson = "{}";
};

enum class EAgentToolResultStatus
{
    Unknown,
    Succeeded,
    Failed,
    Pending
};

enum class EAgentDiagnosticSeverity
{
    Info,
    Warning,
    Error
};

struct FAgentArtifact
{
    std::string Handle;
    std::string Kind;
    std::string Summary;
    std::string RelativePath;
    std::uint64_t SizeBytes = 0;
};

struct FAgentDiagnostic
{
    EAgentDiagnosticSeverity Severity = EAgentDiagnosticSeverity::Info;
    std::string Code;
    std::string Message;
};

struct FAgentRevisionChange
{
    std::string Domain;
    std::uint64_t Before = 0;
    std::uint64_t After = 0;
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
    EAgentFailureClass FailureClass = EAgentFailureClass::None;
    EAgentRecoveryAction RecoveryAction = EAgentRecoveryAction::Abort;
    EAgentToolResultStatus Status = EAgentToolResultStatus::Unknown;
    std::string FactsJson = "{}";
    std::vector<FAgentArtifact> Artifacts;
    std::vector<FAgentDiagnostic> Diagnostics;
    std::vector<std::string> StateChanges;
    std::vector<FAgentRevisionChange> RevisionChanges;
    std::string RecoveryHint;
};

struct FAgentBudget
{
    std::size_t MaxSteps = 16;
    std::size_t MaxToolCalls = 32;
    std::size_t MaxReadOnlyToolCalls = 12;
    std::size_t MaxMutationToolCalls = 20;
    std::size_t MaxConsecutiveNoProgressSteps = 2;
    std::size_t ReservedFinalSteps = 1;
    std::size_t MaxRepairAttempts = 2;
    std::uint64_t MaxElapsedMilliseconds = 30000;
    std::size_t MaxContextMessages = 48;
    std::uint64_t MaxContextBytesPerRequest = 256 * 1024;
};

struct FAgentCounters
{
    std::size_t Steps = 0;
    std::size_t ToolCalls = 0;
    std::size_t ReadOnlyToolCalls = 0;
    std::size_t MutationToolCalls = 0;
    std::size_t SemanticCacheHits = 0;
    std::uint64_t ProviderUsageResponses = 0;
    std::uint64_t ProviderPromptTokens = 0;
    std::uint64_t ProviderCompletionTokens = 0;
    std::uint64_t ProviderCacheHitTokens = 0;
    std::uint64_t ProviderCacheMissTokens = 0;
    std::uint64_t ProviderCacheDetailResponses = 0;
    std::size_t ConsecutiveNoProgressSteps = 0;
    std::size_t Observations = 0;
    std::size_t EvidenceBindings = 0;
    std::size_t OscillationsDetected = 0;
    std::size_t RepairAttempts = 0;
    std::size_t ReflectionAttempts = 0;
    std::size_t RecoveryEscalations = 0;
    std::size_t ContextMessages = 0;
    std::size_t TrimmedContextMessages = 0;
};

struct FAgentContextMetrics
{
    std::uint64_t AssemblyCount = 0;
    std::uint64_t TotalAssemblyMicroseconds = 0;
    std::uint64_t MaxAssemblyMicroseconds = 0;
    std::uint64_t InstructionBytes = 0;
    std::uint64_t TaskStateBytes = 0;
    std::uint64_t ConversationBytes = 0;
    std::uint64_t MemoryBytes = 0;
    std::uint64_t ObservationBytes = 0;
    std::uint64_t DroppedBytes = 0;
    std::uint64_t TotalBytes = 0;
    std::uint64_t ProjectedHistoryBytes = 0;
    std::uint64_t ProjectedMessages = 0;
};

struct FAgentRunResult
{
    EAgentStatus Status = EAgentStatus::Idle;
    std::string FinalText;
    std::string Error;
    FAgentCounters Counters;
    std::string RunId;
    std::uint64_t ContextBytes = 0;
    FAgentContextMetrics ContextMetrics;
    bool bTaskBoundaryProjectionEnabled = false;
    std::string MetricsPath;
    EAgentFailureClass FailureClass = EAgentFailureClass::None;
    EAgentRecoveryAction RecoveryAction = EAgentRecoveryAction::Abort;
};

std::string_view ToString(EAgentRole Role);
std::string_view ToString(EAgentStatus Status);
std::string_view ToString(EAgentEventType Type);
bool TryParseAgentRole(std::string_view Text, EAgentRole& OutRole);
bool TryParseAgentStatus(std::string_view Text, EAgentStatus& OutStatus);
bool TryParseAgentEventType(std::string_view Text, EAgentEventType& OutType);
std::string_view ToString(EAgentFailureClass FailureClass);
std::string_view ToString(EAgentRecoveryAction Action);
std::string_view ToString(EAgentToolResultStatus Status);
std::string_view ToString(EAgentDiagnosticSeverity Severity);
bool TryParseAgentFailureClass(
    std::string_view Text,
    EAgentFailureClass& OutFailureClass);
bool TryParseAgentRecoveryAction(
    std::string_view Text,
    EAgentRecoveryAction& OutAction);
bool TryParseAgentToolResultStatus(
    std::string_view Text,
    EAgentToolResultStatus& OutStatus);
bool TryParseAgentDiagnosticSeverity(
    std::string_view Text,
    EAgentDiagnosticSeverity& OutSeverity);
void NormalizeAgentToolResult(FAgentToolResult& Result);
std::string SerializeAgentToolResult(const FAgentToolResult& Result);
bool DeserializeAgentToolResult(
    std::string_view Json,
    FAgentToolResult& OutResult,
    std::string* OutError = nullptr);
std::string BuildAgentToolResultModelJson(const FAgentToolResult& Result);
FAgentRecoveryPolicy GetAgentRecoveryPolicy(EAgentFailureClass FailureClass);
bool IsAllowedAgentTransition(EAgentStatus From, EAgentStatus To);
}
