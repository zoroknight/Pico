#include "Pico/Agent/AgentTypes.h"

namespace Pico
{
namespace
{
template <typename T>
bool ParseEnum(
    std::string_view Text,
    const std::initializer_list<std::pair<std::string_view, T>>& Values,
    T& OutValue)
{
    for (const auto& [Name, Value] : Values)
    {
        if (Text == Name)
        {
            OutValue = Value;
            return true;
        }
    }
    return false;
}
}

std::string_view ToString(EAgentRole Role)
{
    switch (Role)
    {
    case EAgentRole::System: return "System";
    case EAgentRole::User: return "User";
    case EAgentRole::Assistant: return "Assistant";
    case EAgentRole::Tool: return "Tool";
    }
    return "User";
}

std::string_view ToString(EAgentStatus Status)
{
    switch (Status)
    {
    case EAgentStatus::Idle: return "Idle";
    case EAgentStatus::Planning: return "Planning";
    case EAgentStatus::AwaitingApproval: return "AwaitingApproval";
    case EAgentStatus::ExecutingTool: return "ExecutingTool";
    case EAgentStatus::Validating: return "Validating";
    case EAgentStatus::Repairing: return "Repairing";
    case EAgentStatus::Completed: return "Completed";
    case EAgentStatus::Failed: return "Failed";
    case EAgentStatus::Cancelled: return "Cancelled";
    }
    return "Idle";
}

std::string_view ToString(EAgentEventType Type)
{
    switch (Type)
    {
    case EAgentEventType::SessionCreated: return "SessionCreated";
    case EAgentEventType::StatusChanged: return "StatusChanged";
    case EAgentEventType::Message: return "Message";
    case EAgentEventType::ToolCall: return "ToolCall";
    case EAgentEventType::ToolResult: return "ToolResult";
    case EAgentEventType::TraceSpan: return "TraceSpan";
    case EAgentEventType::Checkpoint: return "Checkpoint";
    case EAgentEventType::Error: return "Error";
    }
    return "Message";
}

std::string_view ToString(EAgentFailureClass FailureClass)
{
    switch (FailureClass)
    {
    case EAgentFailureClass::None: return "None";
    case EAgentFailureClass::ModelProtocol: return "ModelProtocol";
    case EAgentFailureClass::InvalidArguments: return "InvalidArguments";
    case EAgentFailureClass::PermissionDenied: return "PermissionDenied";
    case EAgentFailureClass::ApprovalRejected: return "ApprovalRejected";
    case EAgentFailureClass::PreconditionFailed: return "PreconditionFailed";
    case EAgentFailureClass::ExecutionFailed: return "ExecutionFailed";
    case EAgentFailureClass::VerificationFailed: return "VerificationFailed";
    case EAgentFailureClass::Infrastructure: return "Infrastructure";
    case EAgentFailureClass::BudgetExceeded: return "BudgetExceeded";
    case EAgentFailureClass::Conflict: return "Conflict";
    case EAgentFailureClass::Cancelled: return "Cancelled";
    }
    return "Infrastructure";
}

std::string_view ToString(EAgentRecoveryAction Action)
{
    switch (Action)
    {
    case EAgentRecoveryAction::Retry: return "Retry";
    case EAgentRecoveryAction::RefreshState: return "RefreshState";
    case EAgentRecoveryAction::Replan: return "Replan";
    case EAgentRecoveryAction::WaitForApproval: return "WaitForApproval";
    case EAgentRecoveryAction::AskUser: return "AskUser";
    case EAgentRecoveryAction::Rollback: return "Rollback";
    case EAgentRecoveryAction::Abort: return "Abort";
    }
    return "Abort";
}

bool TryParseAgentRole(std::string_view Text, EAgentRole& OutRole)
{
    return ParseEnum(Text, {{"System", EAgentRole::System}, {"User", EAgentRole::User},
        {"Assistant", EAgentRole::Assistant}, {"Tool", EAgentRole::Tool}}, OutRole);
}

bool TryParseAgentStatus(std::string_view Text, EAgentStatus& OutStatus)
{
    return ParseEnum(Text, {{"Idle", EAgentStatus::Idle}, {"Planning", EAgentStatus::Planning},
        {"AwaitingApproval", EAgentStatus::AwaitingApproval},
        {"ExecutingTool", EAgentStatus::ExecutingTool}, {"Validating", EAgentStatus::Validating},
        {"Repairing", EAgentStatus::Repairing}, {"Completed", EAgentStatus::Completed},
        {"Failed", EAgentStatus::Failed}, {"Cancelled", EAgentStatus::Cancelled}}, OutStatus);
}

bool TryParseAgentEventType(std::string_view Text, EAgentEventType& OutType)
{
    return ParseEnum(Text, {{"SessionCreated", EAgentEventType::SessionCreated},
        {"StatusChanged", EAgentEventType::StatusChanged}, {"Message", EAgentEventType::Message},
        {"ToolCall", EAgentEventType::ToolCall}, {"ToolResult", EAgentEventType::ToolResult},
        {"TraceSpan", EAgentEventType::TraceSpan},
        {"Checkpoint", EAgentEventType::Checkpoint}, {"Error", EAgentEventType::Error}}, OutType);
}

bool TryParseAgentFailureClass(
    std::string_view Text,
    EAgentFailureClass& OutFailureClass)
{
    return ParseEnum(Text, {{"None", EAgentFailureClass::None},
        {"ModelProtocol", EAgentFailureClass::ModelProtocol},
        {"InvalidArguments", EAgentFailureClass::InvalidArguments},
        {"PermissionDenied", EAgentFailureClass::PermissionDenied},
        {"ApprovalRejected", EAgentFailureClass::ApprovalRejected},
        {"PreconditionFailed", EAgentFailureClass::PreconditionFailed},
        {"ExecutionFailed", EAgentFailureClass::ExecutionFailed},
        {"VerificationFailed", EAgentFailureClass::VerificationFailed},
        {"Infrastructure", EAgentFailureClass::Infrastructure},
        {"BudgetExceeded", EAgentFailureClass::BudgetExceeded},
        {"Conflict", EAgentFailureClass::Conflict},
        {"Cancelled", EAgentFailureClass::Cancelled}}, OutFailureClass);
}

bool TryParseAgentRecoveryAction(
    std::string_view Text,
    EAgentRecoveryAction& OutAction)
{
    return ParseEnum(Text, {{"Retry", EAgentRecoveryAction::Retry},
        {"RefreshState", EAgentRecoveryAction::RefreshState},
        {"Replan", EAgentRecoveryAction::Replan},
        {"WaitForApproval", EAgentRecoveryAction::WaitForApproval},
        {"AskUser", EAgentRecoveryAction::AskUser},
        {"Rollback", EAgentRecoveryAction::Rollback},
        {"Abort", EAgentRecoveryAction::Abort}}, OutAction);
}

FAgentRecoveryPolicy GetAgentRecoveryPolicy(EAgentFailureClass FailureClass)
{
    switch (FailureClass)
    {
    case EAgentFailureClass::None:
        return {EAgentRecoveryAction::Abort, false};
    case EAgentFailureClass::ModelProtocol:
        return {EAgentRecoveryAction::Replan, true};
    case EAgentFailureClass::InvalidArguments:
        return {EAgentRecoveryAction::Replan, false};
    case EAgentFailureClass::PermissionDenied:
        return {EAgentRecoveryAction::AskUser, false};
    case EAgentFailureClass::ApprovalRejected:
        return {EAgentRecoveryAction::WaitForApproval, false};
    case EAgentFailureClass::PreconditionFailed:
        return {EAgentRecoveryAction::RefreshState, true};
    case EAgentFailureClass::ExecutionFailed:
        return {EAgentRecoveryAction::Retry, true};
    case EAgentFailureClass::VerificationFailed:
        return {EAgentRecoveryAction::Rollback, false};
    case EAgentFailureClass::Infrastructure:
        return {EAgentRecoveryAction::Retry, true};
    case EAgentFailureClass::BudgetExceeded:
        return {EAgentRecoveryAction::Abort, false};
    case EAgentFailureClass::Conflict:
        return {EAgentRecoveryAction::RefreshState, true};
    case EAgentFailureClass::Cancelled:
        return {EAgentRecoveryAction::Abort, false};
    }
    return {EAgentRecoveryAction::Abort, false};
}

bool IsAllowedAgentTransition(EAgentStatus From, EAgentStatus To)
{
    if (From == To) return true;
    if (To == EAgentStatus::Failed || To == EAgentStatus::Cancelled) return true;
    switch (From)
    {
    case EAgentStatus::Idle:
    case EAgentStatus::Completed:
    case EAgentStatus::Failed:
    case EAgentStatus::Cancelled:
        return To == EAgentStatus::Planning;
    case EAgentStatus::Planning:
        return To == EAgentStatus::AwaitingApproval
            || To == EAgentStatus::ExecutingTool
            || To == EAgentStatus::Repairing
            || To == EAgentStatus::Completed;
    case EAgentStatus::AwaitingApproval:
        return To == EAgentStatus::ExecutingTool;
    case EAgentStatus::ExecutingTool:
        return To == EAgentStatus::Validating || To == EAgentStatus::Repairing;
    case EAgentStatus::Validating:
        return To == EAgentStatus::Planning || To == EAgentStatus::Repairing
            || To == EAgentStatus::Completed;
    case EAgentStatus::Repairing:
        return To == EAgentStatus::Planning;
    }
    return false;
}
}
