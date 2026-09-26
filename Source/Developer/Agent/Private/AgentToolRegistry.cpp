#include "Pico/Agent/AgentToolRegistry.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

bool MatchesType(const FJson& Value, EAgentToolValueType Type)
{
    switch (Type)
    {
    case EAgentToolValueType::String: return Value.is_string();
    case EAgentToolValueType::Number: return Value.is_number();
    case EAgentToolValueType::Integer: return Value.is_number_integer();
    case EAgentToolValueType::Boolean: return Value.is_boolean();
    case EAgentToolValueType::Object: return Value.is_object();
    case EAgentToolValueType::Array: return Value.is_array();
    }
    return false;
}

std::string_view JsonTypeName(EAgentToolValueType Type)
{
    switch (Type)
    {
    case EAgentToolValueType::String: return "string";
    case EAgentToolValueType::Number: return "number";
    case EAgentToolValueType::Integer: return "integer";
    case EAgentToolValueType::Boolean: return "boolean";
    case EAgentToolValueType::Object: return "object";
    case EAgentToolValueType::Array: return "array";
    }
    return "string";
}

bool IsValidToolName(std::string_view Name)
{
    if (Name.empty() || Name.size() > 96) return false;
    return std::all_of(Name.begin(), Name.end(), [](const unsigned char Character)
    {
        return std::isalnum(Character) || Character == '_' || Character == '-'
            || Character == '.';
    });
}

bool IsSafeProjectRelativePath(
    std::string_view Text,
    const std::filesystem::path& ProjectRoot)
{
    const std::filesystem::path Relative(Text);
    if (ProjectRoot.empty() || Relative.empty() || Relative.is_absolute()
        || Relative.has_root_name() || Relative.has_root_directory())
    {
        return false;
    }
    for (const auto& Segment : Relative)
    {
        if (Segment == "..") return false;
    }
    const std::filesystem::path Root = std::filesystem::weakly_canonical(ProjectRoot);
    const std::filesystem::path Candidate = std::filesystem::weakly_canonical(Root / Relative);
    auto RootIt = Root.begin();
    auto CandidateIt = Candidate.begin();
    for (; RootIt != Root.end(); ++RootIt, ++CandidateIt)
    {
        if (CandidateIt == Candidate.end() || *RootIt != *CandidateIt) return false;
    }
    return true;
}
}

bool FAgentToolPolicy::Allows(EAgentToolPermission Permission) const
{
    switch (Permission)
    {
    case EAgentToolPermission::ReadOnly: return bAllowReadOnly;
    case EAgentToolPermission::ModifyWorld: return bAllowModifyWorld;
    case EAgentToolPermission::WriteProject: return bAllowWriteProject;
    case EAgentToolPermission::LaunchProcess: return bAllowLaunchProcess;
    }
    return false;
}

bool FAgentToolPolicy::RequiresApproval(EAgentToolPermission Permission) const
{
    switch (Permission)
    {
    case EAgentToolPermission::ReadOnly: return false;
    case EAgentToolPermission::ModifyWorld: return bRequireApprovalForModifyWorld;
    case EAgentToolPermission::WriteProject: return bRequireApprovalForWriteProject;
    case EAgentToolPermission::LaunchProcess: return bRequireApprovalForLaunchProcess;
    }
    return true;
}

FAgentToolRegistry::FAgentToolRegistry(
    FAgentToolPolicy InPolicy,
    IAgentToolApproval* InApproval,
    IAgentToolTransaction* InTransaction)
    : Policy(std::move(InPolicy))
    , Approval(InApproval)
    , Transaction(InTransaction)
{
}

bool FAgentToolRegistry::Register(
    FAgentToolDefinition Definition,
    std::string* OutError)
{
    if (!IsValidToolName(Definition.Name) || !Definition.Handler)
    {
        if (OutError) *OutError = "Tool name is invalid or handler is missing";
        return false;
    }
    std::unordered_set<std::string> FieldNames;
    for (const FAgentToolFieldSchema& Field : Definition.Schema.Fields)
    {
        if (Field.Name.empty() || !FieldNames.insert(Field.Name).second)
        {
            if (OutError) *OutError = "Tool schema field names must be unique and non-empty";
            return false;
        }
        if (Field.Minimum && Field.Maximum && *Field.Minimum > *Field.Maximum)
        {
            if (OutError) *OutError = "Tool schema minimum exceeds maximum";
            return false;
        }
    }
    if (Definitions.contains(Definition.Name))
    {
        if (OutError) *OutError = "Tool is already registered";
        return false;
    }
    Definitions.emplace(Definition.Name, std::move(Definition));
    return true;
}

bool FAgentToolRegistry::RegisterProvider(
    const IAgentCapabilityProvider& Provider,
    std::string* OutError)
{
    if (Provider.GetName().empty())
    {
        if (OutError) *OutError = "Capability provider name is empty";
        return false;
    }
    std::vector<std::string> RegisteredNames;
    for (FAgentToolDefinition Definition : Provider.GetToolDefinitions())
    {
        if (Definition.CapabilityProvider.empty())
            Definition.CapabilityProvider = Provider.GetName();
        if (Definition.CapabilityProvider != Provider.GetName())
        {
            if (OutError) *OutError = "Tool capability provider ownership mismatch";
            for (const std::string& Name : RegisteredNames) Definitions.erase(Name);
            return false;
        }
        const std::string ToolName = Definition.Name;
        if (!Register(std::move(Definition), OutError))
        {
            for (const std::string& Name : RegisteredNames) Definitions.erase(Name);
            return false;
        }
        RegisteredNames.push_back(ToolName);
    }
    return true;
}

bool FAgentToolRegistry::Contains(std::string_view Name) const
{
    return Find(Name) != nullptr;
}

std::vector<std::string> FAgentToolRegistry::GetToolNames() const
{
    std::vector<std::string> Names;
    Names.reserve(Definitions.size());
    for (const auto& [Name, Definition] : Definitions)
    {
        (void)Definition;
        Names.push_back(Name);
    }
    std::sort(Names.begin(), Names.end());
    return Names;
}

std::string FAgentToolRegistry::BuildToolCatalogJson() const
{
    FJson Catalog = FJson::array();
    for (const std::string& Name : GetToolNames())
    {
        const FAgentToolDefinition& Definition = Definitions.at(Name);
        FJson Properties = FJson::object();
        FJson Required = FJson::array();
        for (const FAgentToolFieldSchema& Field : Definition.Schema.Fields)
        {
            FJson FieldJson {{"type", JsonTypeName(Field.Type)}};
            if (Field.Minimum) FieldJson["minimum"] = *Field.Minimum;
            if (Field.Maximum) FieldJson["maximum"] = *Field.Maximum;
            if (Field.MaxLength > 0) FieldJson["maxLength"] = Field.MaxLength;
            if (Field.StringFormat == EAgentToolStringFormat::AssetPath)
                FieldJson["format"] = "pico-asset-path";
            if (Field.StringFormat == EAgentToolStringFormat::ProjectRelativePath)
                FieldJson["format"] = "pico-project-relative-path";
            Properties[Field.Name] = std::move(FieldJson);
            if (Field.bRequired) Required.push_back(Field.Name);
        }
        Catalog.push_back({
            {"name", Definition.Name}, {"description", Definition.Description},
            {"permission", ToString(Definition.Permission)},
            {"capability_provider", Definition.CapabilityProvider},
            {"revision_read_set", Definition.RevisionReadSet},
            {"revision_write_set", Definition.RevisionWriteSet},
            {"input_schema", {{"type", "object"}, {"properties", Properties},
                {"required", Required},
                {"additionalProperties", Definition.Schema.bAllowAdditionalFields}}}
        });
    }
    return Catalog.dump();
}

bool FAgentToolRegistry::RequiresApproval(const FAgentToolCall& Call) const
{
    const FAgentToolDefinition* Definition = Find(Call.Name);
    return Definition && Policy.RequiresApproval(Definition->Permission);
}

bool FAgentToolRegistry::IsReadOnly(const FAgentToolCall& Call) const
{
    const FAgentToolDefinition* Definition = Find(Call.Name);
    return Definition && Definition->Permission == EAgentToolPermission::ReadOnly;
}

std::vector<std::string> FAgentToolRegistry::GetRevisionReadSet(
    const FAgentToolCall& Call) const
{
    const FAgentToolDefinition* Definition = Find(Call.Name);
    return Definition != nullptr && !Definition->RevisionReadSet.empty()
        ? Definition->RevisionReadSet : std::vector<std::string>{"State.Revision"};
}

std::vector<std::string> FAgentToolRegistry::GetRevisionWriteSet(
    const FAgentToolCall& Call) const
{
    const FAgentToolDefinition* Definition = Find(Call.Name);
    return Definition != nullptr && !Definition->RevisionWriteSet.empty()
        ? Definition->RevisionWriteSet : std::vector<std::string>{"State.Revision"};
}

void FAgentToolRegistry::PrepareApproval(const FAgentToolCall& Call)
{
    const FAgentToolDefinition* Definition = Find(Call.Name);
    std::string Error;
    if (!Definition || !Validate(Call, *Definition, Error)
        || !Policy.Allows(Definition->Permission)
        || !Policy.RequiresApproval(Definition->Permission))
    {
        return;
    }
    const bool bApproved = Approval
        && Approval->RequestApproval(Call, Definition->Permission, Definition->Description);
    PreparedApprovals[Call.Id] = {Call.Name + "\n" + Call.ArgumentsJson, bApproved};
}

bool FAgentToolRegistry::PrepareApprovalDecision(
    const FAgentToolCall& Call, bool bApproved)
{
    const FAgentToolDefinition* Definition = Find(Call.Name);
    std::string Error;
    if (!Definition || !Validate(Call, *Definition, Error)
        || !Policy.Allows(Definition->Permission)
        || !Policy.RequiresApproval(Definition->Permission))
    {
        return false;
    }
    PreparedApprovals[Call.Id] = {
        Call.Name + "\n" + Call.ArgumentsJson, bApproved};
    return true;
}

FAgentToolResult FAgentToolRegistry::Execute(
    const FAgentToolCall& Call,
    const FCancellationToken* CancellationToken)
{
    LastTrace.clear();
    const FAgentToolDefinition* Definition = Find(Call.Name);
    if (!Definition)
    {
        Trace(EAgentToolStage::Validate, false, "Unknown tool: " + Call.Name);
        return Failure(Call, "Unknown tool: " + Call.Name,
            EAgentFailureClass::ModelProtocol);
    }

    std::string Error;
    if (!Validate(Call, *Definition, Error))
    {
        Trace(EAgentToolStage::Validate, false, Error);
        return Failure(Call, std::move(Error),
            EAgentFailureClass::InvalidArguments);
    }
    Trace(EAgentToolStage::Validate, true, "Arguments match schema");

    if (!Policy.Allows(Definition->Permission))
    {
        Error = "Permission denied for " + std::string(ToString(Definition->Permission));
        Trace(EAgentToolStage::Permission, false, Error);
        return Failure(Call, std::move(Error),
            EAgentFailureClass::PermissionDenied);
    }
    Trace(EAgentToolStage::Permission, true, "Permission allowed by policy");

    if (Policy.RequiresApproval(Definition->Permission))
    {
        auto Prepared = PreparedApprovals.find(Call.Id);
        const std::string Fingerprint = Call.Name + "\n" + Call.ArgumentsJson;
        const bool bHasMatchingPreparedApproval = Prepared != PreparedApprovals.end()
            && Prepared->second.Fingerprint == Fingerprint;
        const bool bApproved = bHasMatchingPreparedApproval
            ? Prepared->second.bApproved
            : Approval && Approval->RequestApproval(
                Call, Definition->Permission, Definition->Description);
        if (Prepared != PreparedApprovals.end()) PreparedApprovals.erase(Prepared);
        if (!bApproved)
        {
            Trace(EAgentToolStage::Approval, false, "User denied tool call");
            return Failure(Call, "User denied tool call",
                EAgentFailureClass::ApprovalRejected);
        }
        Trace(EAgentToolStage::Approval, true, "User approved tool call");
    }
    else
    {
        Trace(EAgentToolStage::Approval, true, "Approval not required");
    }

    if (CancellationToken && CancellationToken->IsCancellationRequested())
    {
        return Failure(Call, "Tool call was cancelled before execution",
            EAgentFailureClass::Cancelled);
    }

    if (Definition->Preflight && !Definition->Preflight(Call, Error))
    {
        if (Error.empty()) Error = "Tool precondition was not satisfied";
        Trace(EAgentToolStage::Execute, false, Error);
        return Failure(Call, std::move(Error),
            EAgentFailureClass::PreconditionFailed);
    }

    const bool bTransactional =
        Definition->Permission == EAgentToolPermission::ModifyWorld;
    if (bTransactional)
    {
        if (!Transaction || !Transaction->Begin(Definition->Description, Error))
        {
            if (Error.empty()) Error = "Could not begin tool transaction";
            Trace(EAgentToolStage::Transaction, false, Error);
            return Failure(Call, std::move(Error),
                EAgentFailureClass::Infrastructure);
        }
        Trace(EAgentToolStage::Transaction, true, "Transaction started");
    }
    else
    {
        Trace(EAgentToolStage::Transaction, true,
            Definition->Permission == EAgentToolPermission::ReadOnly
                ? "Read-only tool needs no transaction"
                : "Project/process tool uses its own operation boundary");
    }

    FAgentToolResult Result;
    try
    {
        Result = Definition->Handler(Call, CancellationToken);
        Result.CallId = Call.Id;
        if (Result.bSucceeded)
        {
            const auto ParsedOutput = FJson::parse(Result.OutputJson);
            (void)ParsedOutput;
        }
        NormalizeAgentToolResult(Result);
    }
    catch (const std::exception& Exception)
    {
        Result = Failure(Call, Exception.what(),
            EAgentFailureClass::ExecutionFailed);
    }
    catch (...)
    {
        Result = Failure(Call, "Tool handler threw an unknown exception",
            EAgentFailureClass::ExecutionFailed);
    }

    if (!Result.bSucceeded)
    {
        if (Result.FailureClass == EAgentFailureClass::None)
        {
            Result.FailureClass = EAgentFailureClass::ExecutionFailed;
            Result.RecoveryAction = GetAgentRecoveryPolicy(
                Result.FailureClass).Action;
        }
        NormalizeAgentToolResult(Result);
        Trace(EAgentToolStage::Execute, false,
            Result.Error.empty() ? "Tool execution failed" : Result.Error);
        if (bTransactional)
        {
            std::string RollbackError;
            const bool bRolledBack = Transaction->Rollback(RollbackError);
            Trace(EAgentToolStage::Transaction, bRolledBack,
                bRolledBack ? "Transaction rolled back" : RollbackError);
        }
        return Result;
    }
    Trace(EAgentToolStage::Execute, true, "Tool handler completed");

    if (Definition->Verifier
        && !Definition->Verifier(Call, Result, Error))
    {
        Trace(EAgentToolStage::Verify, false, Error);
        if (bTransactional)
        {
            std::string RollbackError;
            const bool bRolledBack = Transaction->Rollback(RollbackError);
            Trace(EAgentToolStage::Transaction, bRolledBack,
                bRolledBack ? "Transaction rolled back" : RollbackError);
        }
        return Failure(Call, Error.empty() ? "Tool verification failed" : Error,
            EAgentFailureClass::VerificationFailed);
    }
    Result.bPostconditionVerified = Result.bPostconditionVerified
        || Definition->bVerifierChecksPostcondition;
    Trace(EAgentToolStage::Verify, true,
        Result.bPostconditionVerified ? "Postcondition verified"
            : "Basic success check only; target readback is still required");

    if (bTransactional)
    {
        if (!Transaction->Commit(Error))
        {
            std::string RollbackError;
            Transaction->Rollback(RollbackError);
            Trace(EAgentToolStage::Transaction, false,
                Error.empty() ? "Could not commit tool transaction" : Error);
            return Failure(Call, Error.empty() ? "Could not commit tool transaction" : Error,
                EAgentFailureClass::Infrastructure);
        }
        Trace(EAgentToolStage::Transaction, true, "Transaction committed");
    }
    for (const std::string& Domain : Definition->RevisionWriteSet)
    {
        Result.RevisionChanges.push_back({Domain, 0, 0});
    }
    NormalizeAgentToolResult(Result);
    return Result;
}

const std::vector<FAgentToolStageTrace>& FAgentToolRegistry::GetLastTrace() const
{
    return LastTrace;
}

std::string FAgentToolRegistry::GetLastExecutionTraceJson() const
{
    FJson TraceJson = FJson::array();
    for (const FAgentToolStageTrace& Entry : LastTrace)
    {
        TraceJson.push_back({{"stage", ToString(Entry.Stage)},
            {"succeeded", Entry.bSucceeded}, {"message", Entry.Message}});
    }
    return TraceJson.dump();
}

const FAgentToolDefinition* FAgentToolRegistry::Find(std::string_view Name) const
{
    const auto It = Definitions.find(std::string(Name));
    return It != Definitions.end() ? &It->second : nullptr;
}

bool FAgentToolRegistry::Validate(
    const FAgentToolCall& Call,
    const FAgentToolDefinition& Definition,
    std::string& OutError) const
{
    FJson Arguments;
    try
    {
        Arguments = FJson::parse(Call.ArgumentsJson);
    }
    catch (const std::exception& Exception)
    {
        OutError = "Arguments are not valid JSON: " + std::string(Exception.what());
        return false;
    }
    if (!Arguments.is_object())
    {
        OutError = "Tool arguments must be a JSON object";
        return false;
    }

    std::unordered_set<std::string> KnownFields;
    for (const FAgentToolFieldSchema& Field : Definition.Schema.Fields)
    {
        KnownFields.insert(Field.Name);
        if (!Arguments.contains(Field.Name))
        {
            if (Field.bRequired)
            {
                OutError = "Missing required field: " + Field.Name;
                return false;
            }
            continue;
        }
        const FJson& Value = Arguments.at(Field.Name);
        if (!MatchesType(Value, Field.Type))
        {
            OutError = "Field has wrong type: " + Field.Name;
            return false;
        }
        if (Value.is_number())
        {
            const double Number = Value.get<double>();
            if (!std::isfinite(Number)
                || (Field.Minimum && Number < *Field.Minimum)
                || (Field.Maximum && Number > *Field.Maximum))
            {
                OutError = "Field is outside its numeric range: " + Field.Name;
                return false;
            }
        }
        if (Value.is_string())
        {
            const std::string Text = Value.get<std::string>();
            if (Field.MaxLength > 0 && Text.size() > Field.MaxLength)
            {
                OutError = "Field exceeds maximum length: " + Field.Name;
                return false;
            }
            if (Field.StringFormat == EAgentToolStringFormat::AssetPath)
            {
                FAssetPath AssetPath;
                if (!FAssetPath::TryParse(Text, AssetPath))
                {
                    OutError = "Field is not a valid /Game asset path: " + Field.Name;
                    return false;
                }
            }
            if (Field.StringFormat == EAgentToolStringFormat::ProjectRelativePath
                && !IsSafeProjectRelativePath(Text, Policy.ProjectRoot))
            {
                OutError = "Field escapes the project root: " + Field.Name;
                return false;
            }
        }
    }
    if (!Definition.Schema.bAllowAdditionalFields)
    {
        for (auto It = Arguments.begin(); It != Arguments.end(); ++It)
        {
            if (!KnownFields.contains(It.key()))
            {
                OutError = "Unknown argument field: " + It.key();
                return false;
            }
        }
    }
    return true;
}

void FAgentToolRegistry::Trace(
    EAgentToolStage Stage,
    bool bSucceeded,
    std::string Message)
{
    LastTrace.push_back({Stage, bSucceeded, std::move(Message)});
}

FAgentToolResult FAgentToolRegistry::Failure(
    const FAgentToolCall& Call,
    std::string Error,
    EAgentFailureClass FailureClass)
{
    FAgentToolResult Result {Call.Id, false, "{}", std::move(Error), false,
        FailureClass, GetAgentRecoveryPolicy(FailureClass).Action};
    NormalizeAgentToolResult(Result);
    return Result;
}

std::string_view ToString(EAgentToolPermission Permission)
{
    switch (Permission)
    {
    case EAgentToolPermission::ReadOnly: return "ReadOnly";
    case EAgentToolPermission::ModifyWorld: return "ModifyWorld";
    case EAgentToolPermission::WriteProject: return "WriteProject";
    case EAgentToolPermission::LaunchProcess: return "LaunchProcess";
    }
    return "ReadOnly";
}

std::string_view ToString(EAgentToolStage Stage)
{
    switch (Stage)
    {
    case EAgentToolStage::Validate: return "Validate";
    case EAgentToolStage::Permission: return "Permission";
    case EAgentToolStage::Approval: return "Approval";
    case EAgentToolStage::Transaction: return "Transaction";
    case EAgentToolStage::Execute: return "Execute";
    case EAgentToolStage::Verify: return "Verify";
    }
    return "Validate";
}
}
