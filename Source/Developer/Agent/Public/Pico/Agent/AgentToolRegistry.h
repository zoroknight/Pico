#pragma once

#include "Pico/Agent/AgentKnowledgeStore.h"
#include "Pico/Agent/AgentProvider.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Pico
{
enum class EAgentToolValueType
{
    String,
    Number,
    Integer,
    Boolean,
    Object,
    Array
};

enum class EAgentToolStringFormat
{
    None,
    AssetPath,
    ProjectRelativePath
};

enum class EAgentToolPermission
{
    ReadOnly,
    ModifyWorld,
    WriteProject,
    LaunchProcess
};

enum class EAgentToolStage
{
    Validate,
    Permission,
    Approval,
    Transaction,
    Execute,
    Verify
};

struct FAgentToolFieldSchema
{
    std::string Name;
    EAgentToolValueType Type = EAgentToolValueType::String;
    bool bRequired = false;
    std::optional<double> Minimum;
    std::optional<double> Maximum;
    std::size_t MaxLength = 0;
    EAgentToolStringFormat StringFormat = EAgentToolStringFormat::None;
};

struct FAgentToolSchema
{
    std::vector<FAgentToolFieldSchema> Fields;
    bool bAllowAdditionalFields = false;
};

struct FAgentToolStageTrace
{
    EAgentToolStage Stage = EAgentToolStage::Validate;
    bool bSucceeded = false;
    std::string Message;
};

struct FAgentToolPolicy
{
    bool bAllowReadOnly = true;
    bool bAllowModifyWorld = false;
    bool bAllowWriteProject = false;
    bool bAllowLaunchProcess = false;
    bool bRequireApprovalForModifyWorld = true;
    bool bRequireApprovalForWriteProject = true;
    bool bRequireApprovalForLaunchProcess = true;
    std::filesystem::path ProjectRoot;

    bool Allows(EAgentToolPermission Permission) const;
    bool RequiresApproval(EAgentToolPermission Permission) const;
};

class IAgentToolApproval
{
public:
    virtual ~IAgentToolApproval() = default;
    virtual bool RequestApproval(
        const FAgentToolCall& Call,
        EAgentToolPermission Permission,
        std::string_view Description) = 0;
};

class IAgentToolTransaction
{
public:
    virtual ~IAgentToolTransaction() = default;
    virtual bool Begin(std::string_view Description, std::string& OutError) = 0;
    virtual bool Commit(std::string& OutError) = 0;
    virtual bool Rollback(std::string& OutError) = 0;
};

using FAgentToolHandler = std::function<FAgentToolResult(
    const FAgentToolCall&,
    const FCancellationToken*)>;
using FAgentToolPreflight = std::function<bool(
    const FAgentToolCall&,
    std::string&)>;
using FAgentToolVerifier = std::function<bool(
    const FAgentToolCall&,
    const FAgentToolResult&,
    std::string&)>;

struct FAgentToolDefinition
{
    std::string Name;
    std::string Description;
    EAgentToolPermission Permission = EAgentToolPermission::ReadOnly;
    FAgentToolSchema Schema;
    FAgentToolPreflight Preflight;
    FAgentToolHandler Handler;
    FAgentToolVerifier Verifier;
    bool bVerifierChecksPostcondition = false;
    std::string CapabilityProvider;
    std::vector<std::string> RevisionReadSet;
    std::vector<std::string> RevisionWriteSet;
};

class IAgentCapabilityProvider
{
public:
    virtual ~IAgentCapabilityProvider() = default;
    virtual std::string_view GetName() const = 0;
    virtual const std::vector<FAgentToolDefinition>& GetToolDefinitions() const = 0;
    virtual std::vector<FAgentKnowledgeRecord> CollectKnowledgeRecords() const = 0;
};

class FAgentToolRegistry final : public IAgentToolExecutor
{
public:
    explicit FAgentToolRegistry(
        FAgentToolPolicy Policy = {},
        IAgentToolApproval* Approval = nullptr,
        IAgentToolTransaction* Transaction = nullptr);

    bool Register(FAgentToolDefinition Definition, std::string* OutError = nullptr);
    bool RegisterProvider(
        const IAgentCapabilityProvider& Provider,
        std::string* OutError = nullptr);
    bool Contains(std::string_view Name) const;
    std::vector<std::string> GetToolNames() const;
    std::string BuildToolCatalogJson() const;

    bool RequiresApproval(const FAgentToolCall& Call) const override;
    bool IsReadOnly(const FAgentToolCall& Call) const override;
    std::vector<std::string> GetRevisionReadSet(
        const FAgentToolCall& Call) const override;
    std::vector<std::string> GetRevisionWriteSet(
        const FAgentToolCall& Call) const override;
    void PrepareApproval(const FAgentToolCall& Call) override;
    bool PrepareApprovalDecision(
        const FAgentToolCall& Call, bool bApproved) override;
    std::string GetLastExecutionTraceJson() const override;
    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) override;

    const std::vector<FAgentToolStageTrace>& GetLastTrace() const;

private:
    struct FPreparedApproval
    {
        std::string Fingerprint;
        bool bApproved = false;
    };

    const FAgentToolDefinition* Find(std::string_view Name) const;
    bool Validate(
        const FAgentToolCall& Call,
        const FAgentToolDefinition& Definition,
        std::string& OutError) const;
    void Trace(EAgentToolStage Stage, bool bSucceeded, std::string Message);
    FAgentToolResult Failure(
        const FAgentToolCall& Call,
        std::string Error,
        EAgentFailureClass FailureClass);

    FAgentToolPolicy Policy;
    IAgentToolApproval* Approval = nullptr;
    IAgentToolTransaction* Transaction = nullptr;
    std::unordered_map<std::string, FAgentToolDefinition> Definitions;
    std::unordered_map<std::string, FPreparedApproval> PreparedApprovals;
    std::vector<FAgentToolStageTrace> LastTrace;
};

std::string_view ToString(EAgentToolPermission Permission);
std::string_view ToString(EAgentToolStage Stage);
}
