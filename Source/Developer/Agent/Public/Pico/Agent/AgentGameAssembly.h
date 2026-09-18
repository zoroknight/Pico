#pragma once

#include "Pico/Agent/AgentTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Pico
{
struct FAgentAssetDescriptor
{
    std::string Id;
    std::string Kind;
    std::string VirtualPath;
    std::string SourcePath;
    std::uint64_t SourceRevision = 0;
    std::uint64_t SizeBytes = 0;
    std::vector<std::string> Dependencies;
    std::vector<std::string> Tags;
    std::vector<std::string> EvidenceHandles;
    std::string SummaryJson = "{}";
    std::string Provenance;
    std::string ValidatorId;
};

struct FAgentCapabilityDescriptor
{
    std::string Id;
    std::uint32_t Version = 1;
    std::string DisplayName;
    std::string Category;
    std::string ProducerId;
    std::vector<std::string> Tags;
    std::vector<std::string> RequiredAssetKinds;
    std::vector<std::string> Dependencies;
    std::vector<std::string> Conflicts;
    std::string Authority = "Any";
    std::string SideEffect = "ReadOnly";
    std::string Approval = "None";
    std::string VerifierId;
    std::string Provenance;
};

struct FAgentGameplayRecipe
{
    std::string Id;
    std::uint32_t Version = 1;
    std::string DisplayName;
    std::vector<std::string> RequirementTags;
    std::vector<std::string> CapabilityIds;
    std::string ParametersSchemaJson = "{}";
    std::string VerifierId;
};

struct FAgentGameRequirement
{
    std::string Id;
    std::vector<std::string> Tags;
    bool bRequired = true;
    std::string SourceText;
};

struct FAgentGameSpec
{
    std::uint32_t FormatVersion = 1;
    std::string Id;
    std::string SourcePrompt;
    std::uint32_t PlayerCount = 1;
    bool bNetworked = false;
    bool bPackageWindows = false;
    std::vector<FAgentGameRequirement> Requirements;
    std::vector<std::string> AcceptanceCriteria;
};

enum class EAgentRequirementSupport
{
    Supported,
    MissingDependency,
    Unsupported
};

struct FAgentRequirementDiagnosis
{
    std::string RequirementId;
    EAgentRequirementSupport Support = EAgentRequirementSupport::Unsupported;
    std::vector<std::string> CapabilityIds;
    std::vector<std::string> RecipeIds;
    std::vector<std::string> MissingAssetKinds;
    std::string Reason;
};

struct FAgentSupportDiagnosis
{
    bool bSupported = false;
    std::vector<FAgentRequirementDiagnosis> Requirements;
};

class FAgentCapabilityCatalog
{
public:
    bool AddCapability(
        FAgentCapabilityDescriptor Descriptor,
        std::string* OutError = nullptr);
    bool AddRecipe(FAgentGameplayRecipe Recipe, std::string* OutError = nullptr);
    const FAgentCapabilityDescriptor* FindCapability(std::string_view Id) const;
    const FAgentGameplayRecipe* FindRecipe(std::string_view Id) const;
    const std::vector<FAgentCapabilityDescriptor>& GetCapabilities() const;
    const std::vector<FAgentGameplayRecipe>& GetRecipes() const;
    FAgentSupportDiagnosis Diagnose(
        const FAgentGameSpec& Spec,
        const std::vector<FAgentAssetDescriptor>& Assets) const;
    std::string ToJson() const;

private:
    std::vector<FAgentCapabilityDescriptor> Capabilities;
    std::vector<FAgentGameplayRecipe> Recipes;
};

struct FAgentBuildPlanStep
{
    std::string Id;
    std::string ProducerId;
    std::string Operation;
    std::string ArgumentsJson = "{}";
    std::vector<std::string> Dependencies;
    std::vector<std::string> ExpectedArtifactKinds;
    std::string IdempotencyKey;
    std::string SideEffect = "WriteProject";
    std::string VerifierId;
};

struct FAgentBuildPlan
{
    std::uint32_t FormatVersion = 1;
    std::string Id;
    std::string GameSpecId;
    std::vector<FAgentBuildPlanStep> Steps;
};

struct FAgentBuildPlanDryRun
{
    std::string PlanHash;
    std::vector<std::string> OrderedStepIds;
    std::vector<std::string> Producers;
    std::vector<std::string> SideEffects;
    std::vector<std::string> ExpectedArtifactKinds;
    std::string ReportJson = "{}";
};

struct FAgentAssemblyCheckpoint
{
    std::uint32_t FormatVersion = 1;
    std::string PlanHash;
    std::vector<std::string> CompletedStepIds;
    std::vector<std::string> CompletedIdempotencyKeys;
    std::vector<FAgentArtifact> Artifacts;
    std::string SummaryJson = "{}";
    FAgentCounters Counters;
};

class IAgentArtifactProducer
{
public:
    virtual ~IAgentArtifactProducer() = default;
    virtual std::string_view GetId() const = 0;
    // Producers may only write beneath StagingDirectory. Publishing staged
    // output into the project belongs to IAgentBuildPlanTransaction::Commit.
    virtual bool Produce(
        const FAgentBuildPlanStep& Step,
        const std::filesystem::path& StagingDirectory,
        std::vector<FAgentArtifact>& OutArtifacts,
        std::string& OutError) = 0;
};

class IAgentBuildPlanTransaction
{
public:
    virtual ~IAgentBuildPlanTransaction() = default;
    virtual bool Begin(std::string_view PlanHash, std::string& OutError) = 0;
    virtual bool Commit(std::string& OutError) = 0;
    virtual bool Rollback(std::string& OutError) = 0;
};

struct FAgentBuildPlanExecutionResult
{
    bool bSucceeded = false;
    bool bResumed = false;
    std::string PlanHash;
    std::string FailedStepId;
    std::string Error;
    FAgentAssemblyCheckpoint Checkpoint;
};

class FAgentArtifactStore
{
public:
    explicit FAgentArtifactStore(std::filesystem::path RootDirectory);
    std::optional<FAgentArtifact> PublishText(
        std::string Kind,
        std::string Summary,
        std::string_view Content,
        std::string* OutError = nullptr) const;
    std::optional<std::string> ReadText(
        std::string_view Handle,
        std::string* OutError = nullptr) const;

private:
    std::filesystem::path RootDirectory;
};

class FAgentAssemblyCheckpointStore
{
public:
    explicit FAgentAssemblyCheckpointStore(std::filesystem::path FilePath);
    bool Save(
        const FAgentAssemblyCheckpoint& Checkpoint,
        std::string* OutError = nullptr) const;
    std::optional<FAgentAssemblyCheckpoint> Load(
        std::string* OutError = nullptr) const;

private:
    std::filesystem::path FilePath;
};

bool ValidateAgentAssetDescriptor(
    const FAgentAssetDescriptor& Descriptor,
    std::string* OutError = nullptr);
std::string SerializeAgentAssetDescriptors(
    const std::vector<FAgentAssetDescriptor>& Descriptors);
std::optional<FAgentGameSpec> ParseAgentGameRequirement(
    std::string_view Prompt,
    std::string* OutError = nullptr);
std::string SerializeAgentGameSpec(const FAgentGameSpec& Spec);
std::string SerializeAgentSupportDiagnosis(
    const FAgentSupportDiagnosis& Diagnosis);
std::optional<FAgentBuildPlan> BuildAgentBuildPlan(
    const FAgentGameSpec& Spec,
    const FAgentCapabilityCatalog& Catalog,
    const FAgentSupportDiagnosis& Diagnosis,
    std::string* OutError = nullptr);
bool ValidateAgentBuildPlan(
    const FAgentBuildPlan& Plan,
    std::vector<std::string>* OutOrderedStepIds = nullptr,
    std::string* OutError = nullptr);
std::string HashAgentBuildPlan(const FAgentBuildPlan& Plan);
std::optional<FAgentBuildPlanDryRun> BuildAgentBuildPlanDryRun(
    const FAgentBuildPlan& Plan,
    std::string* OutError = nullptr);
FAgentBuildPlanExecutionResult ExecuteAgentBuildPlan(
    const FAgentBuildPlan& Plan,
    std::string_view ApprovedPlanHash,
    const std::filesystem::path& StagingRoot,
    const std::unordered_map<std::string, IAgentArtifactProducer*>& Producers,
    IAgentBuildPlanTransaction& Transaction,
    const FAgentAssemblyCheckpointStore& CheckpointStore,
    const FAgentAssemblyCheckpoint* ResumeFrom = nullptr);
std::string_view ToString(EAgentRequirementSupport Support);
}
