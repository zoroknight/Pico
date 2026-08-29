#include "Pico/Agent/AgentTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

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

std::string_view ToString(EAgentToolResultStatus Status)
{
    switch (Status)
    {
    case EAgentToolResultStatus::Unknown: return "Unknown";
    case EAgentToolResultStatus::Succeeded: return "Succeeded";
    case EAgentToolResultStatus::Failed: return "Failed";
    case EAgentToolResultStatus::Pending: return "Pending";
    }
    return "Unknown";
}

std::string_view ToString(EAgentDiagnosticSeverity Severity)
{
    switch (Severity)
    {
    case EAgentDiagnosticSeverity::Info: return "Info";
    case EAgentDiagnosticSeverity::Warning: return "Warning";
    case EAgentDiagnosticSeverity::Error: return "Error";
    }
    return "Info";
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

bool TryParseAgentToolResultStatus(
    std::string_view Text,
    EAgentToolResultStatus& OutStatus)
{
    return ParseEnum(Text, {
        {"Unknown", EAgentToolResultStatus::Unknown},
        {"Succeeded", EAgentToolResultStatus::Succeeded},
        {"Failed", EAgentToolResultStatus::Failed},
        {"Pending", EAgentToolResultStatus::Pending}}, OutStatus);
}

bool TryParseAgentDiagnosticSeverity(
    std::string_view Text,
    EAgentDiagnosticSeverity& OutSeverity)
{
    return ParseEnum(Text, {
        {"Info", EAgentDiagnosticSeverity::Info},
        {"Warning", EAgentDiagnosticSeverity::Warning},
        {"Error", EAgentDiagnosticSeverity::Error}}, OutSeverity);
}

void NormalizeAgentToolResult(FAgentToolResult& Result)
{
    if (Result.Status == EAgentToolResultStatus::Unknown)
    {
        Result.Status = Result.bSucceeded
            ? EAgentToolResultStatus::Succeeded : EAgentToolResultStatus::Failed;
    }
    Result.bSucceeded = Result.Status == EAgentToolResultStatus::Succeeded;
    if (Result.FactsJson.empty() || Result.FactsJson == "{}")
    {
        Result.FactsJson = Result.OutputJson.empty() ? "{}" : Result.OutputJson;
    }
    if (Result.OutputJson.empty()) Result.OutputJson = Result.FactsJson;
    if (!Result.bSucceeded && Result.FailureClass == EAgentFailureClass::None)
    {
        Result.FailureClass = EAgentFailureClass::ExecutionFailed;
    }
    if (!Result.bSucceeded)
    {
        Result.RecoveryAction = GetAgentRecoveryPolicy(Result.FailureClass).Action;
        if (!Result.Error.empty()
            && std::none_of(Result.Diagnostics.begin(), Result.Diagnostics.end(),
                [&Result](const FAgentDiagnostic& Diagnostic)
                {
                    return Diagnostic.Message == Result.Error;
                }))
        {
            Result.Diagnostics.push_back(
                {EAgentDiagnosticSeverity::Error,
                    std::string(ToString(Result.FailureClass)), Result.Error});
        }
    }
    if (Result.RecoveryHint.empty() && !Result.bSucceeded)
    {
        Result.RecoveryHint = "Recovery action: "
            + std::string(ToString(Result.RecoveryAction));
    }
}

std::string SerializeAgentToolResult(const FAgentToolResult& Input)
{
    FAgentToolResult Result = Input;
    NormalizeAgentToolResult(Result);
    FJson Artifacts = FJson::array();
    for (const FAgentArtifact& Artifact : Result.Artifacts)
    {
        Artifacts.push_back({{"handle", Artifact.Handle}, {"kind", Artifact.Kind},
            {"summary", Artifact.Summary}, {"relative_path", Artifact.RelativePath},
            {"size_bytes", Artifact.SizeBytes}});
    }
    FJson Diagnostics = FJson::array();
    for (const FAgentDiagnostic& Diagnostic : Result.Diagnostics)
    {
        Diagnostics.push_back({{"severity", ToString(Diagnostic.Severity)},
            {"code", Diagnostic.Code}, {"message", Diagnostic.Message}});
    }
    FJson Revisions = FJson::array();
    for (const FAgentRevisionChange& Change : Result.RevisionChanges)
    {
        Revisions.push_back({{"domain", Change.Domain}, {"before", Change.Before},
            {"after", Change.After}});
    }
    FJson Facts;
    try { Facts = FJson::parse(Result.FactsJson); }
    catch (...) { Facts = Result.FactsJson; }
    return FJson {{"call_id", Result.CallId}, {"status", ToString(Result.Status)},
        {"failure_class", ToString(Result.FailureClass)},
        {"facts", std::move(Facts)}, {"artifacts", std::move(Artifacts)},
        {"diagnostics", std::move(Diagnostics)},
        {"state_changes", Result.StateChanges},
        {"revision_changes", std::move(Revisions)},
        {"recovery_hint", Result.RecoveryHint}, {"reused", Result.bReused}}.dump();
}

bool DeserializeAgentToolResult(
    std::string_view Json,
    FAgentToolResult& OutResult,
    std::string* OutError)
{
    try
    {
        const FJson Root = FJson::parse(Json);
        EAgentToolResultStatus Status = EAgentToolResultStatus::Unknown;
        EAgentFailureClass FailureClass = EAgentFailureClass::None;
        if (!TryParseAgentToolResultStatus(Root.value("status", "Unknown"), Status)
            || !TryParseAgentFailureClass(
                Root.value("failure_class", "None"), FailureClass))
        {
            if (OutError) *OutError = "Structured Tool Result contains an unknown enum";
            return false;
        }
        OutResult.CallId = Root.value("call_id", "");
        OutResult.Status = Status;
        OutResult.bSucceeded = Status == EAgentToolResultStatus::Succeeded;
        OutResult.FailureClass = FailureClass;
        OutResult.RecoveryAction = GetAgentRecoveryPolicy(FailureClass).Action;
        OutResult.FactsJson = Root.value("facts", FJson::object()).dump();
        OutResult.OutputJson = OutResult.FactsJson;
        OutResult.bReused = Root.value("reused", false);
        OutResult.RecoveryHint = Root.value("recovery_hint", "");
        OutResult.StateChanges = Root.value(
            "state_changes", std::vector<std::string> {});
        for (const FJson& Item : Root.value("artifacts", FJson::array()))
        {
            OutResult.Artifacts.push_back({Item.value("handle", ""),
                Item.value("kind", ""), Item.value("summary", ""),
                Item.value("relative_path", ""), Item.value("size_bytes", 0ULL)});
        }
        for (const FJson& Item : Root.value("diagnostics", FJson::array()))
        {
            EAgentDiagnosticSeverity Severity;
            if (!TryParseAgentDiagnosticSeverity(
                    Item.value("severity", "Info"), Severity))
                Severity = EAgentDiagnosticSeverity::Info;
            OutResult.Diagnostics.push_back({Severity, Item.value("code", ""),
                Item.value("message", "")});
        }
        for (const FJson& Item : Root.value("revision_changes", FJson::array()))
        {
            OutResult.RevisionChanges.push_back({Item.value("domain", ""),
                Item.value("before", 0ULL), Item.value("after", 0ULL)});
        }
        if (!OutResult.bSucceeded && !OutResult.Diagnostics.empty())
            OutResult.Error = OutResult.Diagnostics.front().Message;
        NormalizeAgentToolResult(OutResult);
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

std::string BuildAgentToolResultModelJson(const FAgentToolResult& Input)
{
    FAgentToolResult Result = Input;
    NormalizeAgentToolResult(Result);
    FJson Root = FJson::parse(SerializeAgentToolResult(Result));
    for (FJson& Artifact : Root["artifacts"])
    {
        Artifact.erase("relative_path");
    }
    return Root.dump();
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
