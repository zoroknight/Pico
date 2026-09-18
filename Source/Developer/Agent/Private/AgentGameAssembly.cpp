#include "Pico/Agent/AgentGameAssembly.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::string LowerAscii(std::string Text)
{
    std::transform(Text.begin(), Text.end(), Text.begin(),
        [](unsigned char Character)
        {
            return Character < 128
                ? static_cast<char>(std::tolower(Character))
                : static_cast<char>(Character);
        });
    return Text;
}

bool ContainsAny(std::string_view Text, const std::vector<std::string>& Needles)
{
    const std::string Lower = LowerAscii(std::string(Text));
    return std::any_of(Needles.begin(), Needles.end(),
        [&Lower](const std::string& Needle)
        {
            return Lower.find(LowerAscii(Needle)) != std::string::npos;
        });
}

std::string Utf8(const char8_t* Text)
{
    return std::string(reinterpret_cast<const char*>(Text));
}

std::string StableHash(std::string_view Text)
{
    std::uint64_t Hash = 1469598103934665603ULL;
    for (unsigned char Character : Text)
    {
        Hash ^= Character;
        Hash *= 1099511628211ULL;
    }
    std::ostringstream Stream;
    Stream << std::hex << std::setw(16) << std::setfill('0') << Hash;
    return Stream.str();
}

bool ReplaceFile(
    const std::filesystem::path& Staging,
    const std::filesystem::path& Destination,
    std::string& OutError)
{
    std::error_code Error;
    std::filesystem::rename(Staging, Destination, Error);
    if (!Error) return true;
    Error.clear();
    std::filesystem::remove(Destination, Error);
    Error.clear();
    std::filesystem::rename(Staging, Destination, Error);
    if (!Error) return true;
    OutError = "Could not publish file: " + Error.message();
    return false;
}

FJson AssetToJson(const FAgentAssetDescriptor& Value)
{
    return {{"id", Value.Id}, {"kind", Value.Kind},
        {"virtual_path", Value.VirtualPath}, {"source_path", Value.SourcePath},
        {"source_revision", Value.SourceRevision}, {"size_bytes", Value.SizeBytes},
        {"dependencies", Value.Dependencies}, {"tags", Value.Tags},
        {"evidence_handles", Value.EvidenceHandles},
        {"summary", FJson::parse(Value.SummaryJson)},
        {"provenance", Value.Provenance}, {"validator_id", Value.ValidatorId}};
}

FJson CapabilityToJson(const FAgentCapabilityDescriptor& Value)
{
    return {{"id", Value.Id}, {"version", Value.Version},
        {"display_name", Value.DisplayName}, {"category", Value.Category},
        {"producer_id", Value.ProducerId}, {"tags", Value.Tags},
        {"required_asset_kinds", Value.RequiredAssetKinds},
        {"dependencies", Value.Dependencies}, {"conflicts", Value.Conflicts},
        {"authority", Value.Authority}, {"side_effect", Value.SideEffect},
        {"approval", Value.Approval}, {"verifier_id", Value.VerifierId},
        {"provenance", Value.Provenance}};
}

FJson RecipeToJson(const FAgentGameplayRecipe& Value)
{
    return {{"id", Value.Id}, {"version", Value.Version},
        {"display_name", Value.DisplayName},
        {"requirement_tags", Value.RequirementTags},
        {"capability_ids", Value.CapabilityIds},
        {"parameters_schema", FJson::parse(Value.ParametersSchemaJson)},
        {"verifier_id", Value.VerifierId}};
}

FJson StepToJson(const FAgentBuildPlanStep& Value)
{
    return {{"id", Value.Id}, {"producer_id", Value.ProducerId},
        {"operation", Value.Operation},
        {"arguments", FJson::parse(Value.ArgumentsJson)},
        {"dependencies", Value.Dependencies},
        {"expected_artifact_kinds", Value.ExpectedArtifactKinds},
        {"idempotency_key", Value.IdempotencyKey},
        {"side_effect", Value.SideEffect}, {"verifier_id", Value.VerifierId}};
}

FJson PlanToJson(const FAgentBuildPlan& Value)
{
    FJson Steps = FJson::array();
    for (const FAgentBuildPlanStep& Step : Value.Steps)
        Steps.push_back(StepToJson(Step));
    return {{"format_version", Value.FormatVersion}, {"id", Value.Id},
        {"game_spec_id", Value.GameSpecId}, {"steps", std::move(Steps)}};
}

FJson ArtifactToJson(const FAgentArtifact& Value)
{
    return {{"handle", Value.Handle}, {"kind", Value.Kind},
        {"summary", Value.Summary}, {"relative_path", Value.RelativePath},
        {"size_bytes", Value.SizeBytes}};
}

FAgentArtifact ArtifactFromJson(const FJson& Json)
{
    FAgentArtifact Result;
    Result.Handle = Json.value("handle", "");
    Result.Kind = Json.value("kind", "");
    Result.Summary = Json.value("summary", "");
    Result.RelativePath = Json.value("relative_path", "");
    Result.SizeBytes = Json.value("size_bytes", 0ULL);
    return Result;
}

bool HasAllTags(
    const std::vector<std::string>& Required,
    const std::vector<std::string>& Candidate)
{
    return !Required.empty()
        && std::all_of(Required.begin(), Required.end(), [&Candidate](const std::string& Tag)
    {
        return std::find(Candidate.begin(), Candidate.end(), Tag)
            != Candidate.end();
    });
}

void AddRequirement(
    FAgentGameSpec& Spec,
    std::string Id,
    std::vector<std::string> Tags,
    std::string Source)
{
    if (std::none_of(Spec.Requirements.begin(), Spec.Requirements.end(),
        [&Id](const FAgentGameRequirement& Existing) { return Existing.Id == Id; }))
    {
        Spec.Requirements.push_back(
            {std::move(Id), std::move(Tags), true, std::move(Source)});
    }
}
}

bool ValidateAgentAssetDescriptor(
    const FAgentAssetDescriptor& Descriptor,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    if (Descriptor.Id.empty() || Descriptor.Kind.empty()
        || Descriptor.VirtualPath.empty() || Descriptor.Provenance.empty()
        || Descriptor.ValidatorId.empty())
    {
        if (OutError) *OutError =
            "Asset descriptor requires id, kind, virtual path, provenance, and validator";
        return false;
    }
    try
    {
        const FJson ParsedSummary = FJson::parse(Descriptor.SummaryJson);
        (void)ParsedSummary;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = "Asset descriptor summary is invalid JSON: "
            + std::string(Exception.what());
        return false;
    }
    return true;
}

std::string SerializeAgentAssetDescriptors(
    const std::vector<FAgentAssetDescriptor>& Descriptors)
{
    FJson Result = FJson::array();
    for (const FAgentAssetDescriptor& Descriptor : Descriptors)
        Result.push_back(AssetToJson(Descriptor));
    return Result.dump();
}

bool FAgentCapabilityCatalog::AddCapability(
    FAgentCapabilityDescriptor Descriptor,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    if (Descriptor.Id.empty() || Descriptor.Version == 0
        || Descriptor.ProducerId.empty() || Descriptor.VerifierId.empty()
        || Descriptor.Provenance.empty())
    {
        if (OutError) *OutError =
            "Capability requires id, version, producer, verifier, and provenance";
        return false;
    }
    if (FindCapability(Descriptor.Id) != nullptr)
    {
        if (OutError) *OutError = "Duplicate capability id: " + Descriptor.Id;
        return false;
    }
    Capabilities.push_back(std::move(Descriptor));
    std::sort(Capabilities.begin(), Capabilities.end(),
        [](const auto& Left, const auto& Right) { return Left.Id < Right.Id; });
    return true;
}

bool FAgentCapabilityCatalog::AddRecipe(
    FAgentGameplayRecipe Recipe,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    if (Recipe.Id.empty() || Recipe.Version == 0 || Recipe.VerifierId.empty())
    {
        if (OutError) *OutError = "Recipe requires id, version, and verifier";
        return false;
    }
    try
    {
        const FJson ParsedSchema = FJson::parse(Recipe.ParametersSchemaJson);
        (void)ParsedSchema;
    }
    catch (...)
    {
        if (OutError) *OutError = "Recipe parameter schema is invalid JSON";
        return false;
    }
    for (const std::string& CapabilityId : Recipe.CapabilityIds)
    {
        if (FindCapability(CapabilityId) == nullptr)
        {
            if (OutError) *OutError = "Recipe references unknown capability: "
                + CapabilityId;
            return false;
        }
    }
    if (FindRecipe(Recipe.Id) != nullptr)
    {
        if (OutError) *OutError = "Duplicate recipe id: " + Recipe.Id;
        return false;
    }
    Recipes.push_back(std::move(Recipe));
    std::sort(Recipes.begin(), Recipes.end(),
        [](const auto& Left, const auto& Right) { return Left.Id < Right.Id; });
    return true;
}

const FAgentCapabilityDescriptor* FAgentCapabilityCatalog::FindCapability(
    std::string_view Id) const
{
    const auto Found = std::find_if(Capabilities.begin(), Capabilities.end(),
        [Id](const auto& Entry) { return Entry.Id == Id; });
    return Found == Capabilities.end() ? nullptr : &*Found;
}

const FAgentGameplayRecipe* FAgentCapabilityCatalog::FindRecipe(
    std::string_view Id) const
{
    const auto Found = std::find_if(Recipes.begin(), Recipes.end(),
        [Id](const auto& Entry) { return Entry.Id == Id; });
    return Found == Recipes.end() ? nullptr : &*Found;
}

const std::vector<FAgentCapabilityDescriptor>&
FAgentCapabilityCatalog::GetCapabilities() const { return Capabilities; }
const std::vector<FAgentGameplayRecipe>&
FAgentCapabilityCatalog::GetRecipes() const { return Recipes; }

FAgentSupportDiagnosis FAgentCapabilityCatalog::Diagnose(
    const FAgentGameSpec& Spec,
    const std::vector<FAgentAssetDescriptor>& Assets) const
{
    std::set<std::string> AssetKinds;
    for (const FAgentAssetDescriptor& Asset : Assets) AssetKinds.insert(Asset.Kind);
    FAgentSupportDiagnosis Result;
    Result.bSupported = true;
    for (const FAgentGameRequirement& Requirement : Spec.Requirements)
    {
        FAgentRequirementDiagnosis Entry;
        Entry.RequirementId = Requirement.Id;
        std::set<std::string> Missing;
        std::set<std::string> CapabilityIds;
        bool bHasCandidate = false;
        bool bHasReadyCandidate = false;
        for (const FAgentCapabilityDescriptor& Capability : Capabilities)
        {
            if (!HasAllTags(Requirement.Tags, Capability.Tags)) continue;
            bHasCandidate = true;
            CapabilityIds.insert(Capability.Id);
            bool bCapabilityReady = true;
            for (const std::string& Kind : Capability.RequiredAssetKinds)
                if (!AssetKinds.contains(Kind))
                {
                    Missing.insert(Kind);
                    bCapabilityReady = false;
                }
            bHasReadyCandidate = bHasReadyCandidate || bCapabilityReady;
        }
        for (const FAgentGameplayRecipe& Recipe : Recipes)
            if (HasAllTags(Requirement.Tags, Recipe.RequirementTags))
            {
                bHasCandidate = true;
                Entry.RecipeIds.push_back(Recipe.Id);
                bool bRecipeReady = true;
                for (const std::string& CapabilityId : Recipe.CapabilityIds)
                {
                    CapabilityIds.insert(CapabilityId);
                    const FAgentCapabilityDescriptor* Capability =
                        FindCapability(CapabilityId);
                    if (!Capability)
                    {
                        bRecipeReady = false;
                        continue;
                    }
                    for (const std::string& Kind : Capability->RequiredAssetKinds)
                        if (!AssetKinds.contains(Kind))
                        {
                            Missing.insert(Kind);
                            bRecipeReady = false;
                        }
                }
                bHasReadyCandidate = bHasReadyCandidate || bRecipeReady;
            }
        Entry.CapabilityIds.assign(CapabilityIds.begin(), CapabilityIds.end());
        if (!bHasCandidate)
        {
            Entry.Support = EAgentRequirementSupport::Unsupported;
            Entry.Reason = "No registered capability or recipe matches the requirement";
        }
        else if (!bHasReadyCandidate)
        {
            Entry.MissingAssetKinds.assign(Missing.begin(), Missing.end());
            Entry.Support = EAgentRequirementSupport::MissingDependency;
            Entry.Reason = "Matching capabilities require unavailable asset kinds";
        }
        else
        {
            Entry.Support = EAgentRequirementSupport::Supported;
            Entry.Reason = "Matched versioned capabilities and recipes";
        }
        if (Requirement.bRequired
            && Entry.Support != EAgentRequirementSupport::Supported)
            Result.bSupported = false;
        Result.Requirements.push_back(std::move(Entry));
    }
    return Result;
}

std::string FAgentCapabilityCatalog::ToJson() const
{
    FJson CapabilityJson = FJson::array();
    for (const auto& Entry : Capabilities)
        CapabilityJson.push_back(CapabilityToJson(Entry));
    FJson RecipeJson = FJson::array();
    for (const auto& Entry : Recipes) RecipeJson.push_back(RecipeToJson(Entry));
    return FJson{{"format_version", 1}, {"capabilities", CapabilityJson},
        {"recipes", RecipeJson}}.dump();
}

std::optional<FAgentGameSpec> ParseAgentGameRequirement(
    std::string_view Prompt,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    if (Prompt.empty())
    {
        if (OutError) *OutError = "Game requirement prompt is empty";
        return std::nullopt;
    }
    FAgentGameSpec Spec;
    Spec.SourcePrompt = std::string(Prompt);
    Spec.Id = "game-spec-" + StableHash(Prompt);
    const bool bTwoPlayer = ContainsAny(Prompt,
        {"two player", "two-player", "2 player", "coop", "co-op",
         Utf8(u8"\u53cc\u4eba"), Utf8(u8"\u4e24\u4e2a\u89d2\u8272"),
         Utf8(u8"\u4e24\u540d\u73a9\u5bb6")});
    Spec.PlayerCount = bTwoPlayer ? 2U : 1U;
    Spec.bNetworked = bTwoPlayer || ContainsAny(Prompt,
        {"network", "multiplayer", "client", "server",
         Utf8(u8"\u8054\u673a"), Utf8(u8"\u5ba2\u6237\u7aef"),
         Utf8(u8"\u670d\u52a1\u7aef")});
    Spec.bPackageWindows = ContainsAny(Prompt,
        {"package", "windows build", Utf8(u8"\u6253\u5305"),
         Utf8(u8"\u53ef\u6267\u884c")});
    AddRequirement(Spec, "player-control", {"player", "movement"}, "Player control");
    if (bTwoPlayer)
        AddRequirement(Spec, "cooperative-players", {"coop", "network"},
            "Two cooperative players");
    if (ContainsAny(Prompt, {"different ability", "different abilities",
            Utf8(u8"\u5dee\u5f02\u5316\u80fd\u529b"),
            Utf8(u8"\u4e0d\u540c\u80fd\u529b")}))
        AddRequirement(Spec, "different-abilities", {"ability", "coop"},
            "Different player abilities");
    if (ContainsAny(Prompt, {"collect", "coin", "pickup",
            Utf8(u8"\u6536\u96c6"), Utf8(u8"\u91d1\u5e01"),
            Utf8(u8"\u62fe\u53d6")}))
        AddRequirement(Spec, "collectible", {"collectible", "interaction"},
            "Collectible objective");
    if (ContainsAny(Prompt,
            {"door", "gate", Utf8(u8"\u95e8")}))
        AddRequirement(Spec, "owned-door", {"door", "ownership", "interaction"},
            "Player-owned door");
    if (ContainsAny(Prompt, {"mechanism", "pressure plate",
            Utf8(u8"\u673a\u5173"), Utf8(u8"\u538b\u529b\u677f")}))
        AddRequirement(Spec, "environment-mechanism", {"mechanism", "interaction"},
            "Environment mechanism");
    if (ContainsAny(Prompt,
            {"victory", "win", Utf8(u8"\u80dc\u5229")}))
        AddRequirement(Spec, "shared-victory", {"objective", "victory", "authority"},
            "Shared authoritative victory");
    if (Spec.bPackageWindows)
        AddRequirement(Spec, "windows-package", {"package", "windows"},
            "Windows package");
    Spec.AcceptanceCriteria = {"Static validation passes", "Play starts without errors"};
    if (Spec.bNetworked)
        Spec.AcceptanceCriteria.push_back("Server and two clients converge on shared state");
    if (Spec.bPackageWindows)
        Spec.AcceptanceCriteria.push_back("Windows staged executable passes smoke test");
    return Spec;
}

std::string SerializeAgentGameSpec(const FAgentGameSpec& Spec)
{
    FJson Requirements = FJson::array();
    for (const auto& Entry : Spec.Requirements)
        Requirements.push_back({{"id", Entry.Id}, {"tags", Entry.Tags},
            {"required", Entry.bRequired}, {"source_text", Entry.SourceText}});
    return FJson{{"format_version", Spec.FormatVersion}, {"id", Spec.Id},
        {"source_prompt", Spec.SourcePrompt}, {"player_count", Spec.PlayerCount},
        {"networked", Spec.bNetworked}, {"package_windows", Spec.bPackageWindows},
        {"requirements", Requirements},
        {"acceptance_criteria", Spec.AcceptanceCriteria}}.dump();
}

std::string SerializeAgentSupportDiagnosis(
    const FAgentSupportDiagnosis& Diagnosis)
{
    FJson Requirements = FJson::array();
    for (const auto& Entry : Diagnosis.Requirements)
        Requirements.push_back({{"requirement_id", Entry.RequirementId},
            {"support", ToString(Entry.Support)},
            {"capability_ids", Entry.CapabilityIds}, {"recipe_ids", Entry.RecipeIds},
            {"missing_asset_kinds", Entry.MissingAssetKinds}, {"reason", Entry.Reason}});
    return FJson{{"supported", Diagnosis.bSupported},
        {"requirements", Requirements}}.dump();
}

std::optional<FAgentBuildPlan> BuildAgentBuildPlan(
    const FAgentGameSpec& Spec,
    const FAgentCapabilityCatalog& Catalog,
    const FAgentSupportDiagnosis& Diagnosis,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    if (!Diagnosis.bSupported)
    {
        if (OutError) *OutError = "Game spec contains unsupported requirements";
        return std::nullopt;
    }
    FAgentBuildPlan Plan;
    Plan.Id = "build-plan-" + StableHash(SerializeAgentGameSpec(Spec));
    Plan.GameSpecId = Spec.Id;
    std::string PreviousStep;
    std::set<std::string> AddedCapabilities;
    for (const FAgentRequirementDiagnosis& Requirement : Diagnosis.Requirements)
    {
        for (const std::string& CapabilityId : Requirement.CapabilityIds)
        {
            if (!AddedCapabilities.insert(CapabilityId).second) continue;
            const FAgentCapabilityDescriptor* Capability =
                Catalog.FindCapability(CapabilityId);
            if (!Capability) continue;
            FAgentBuildPlanStep Step;
            Step.Id = "step-" + std::to_string(Plan.Steps.size() + 1)
                + "-" + StableHash(CapabilityId).substr(0, 8);
            Step.ProducerId = Capability->ProducerId;
            Step.Operation = Capability->Id;
            Step.ArgumentsJson = FJson{{"game_spec_id", Spec.Id},
                {"requirement_id", Requirement.RequirementId}}.dump();
            if (!PreviousStep.empty()) Step.Dependencies.push_back(PreviousStep);
            Step.ExpectedArtifactKinds = Capability->RequiredAssetKinds.empty()
                ? std::vector<std::string>{"ProjectMutation"}
                : Capability->RequiredAssetKinds;
            Step.IdempotencyKey = Spec.Id + ":" + Capability->Id;
            Step.SideEffect = Capability->SideEffect;
            Step.VerifierId = Capability->VerifierId;
            PreviousStep = Step.Id;
            Plan.Steps.push_back(std::move(Step));
        }
    }
    if (Plan.Steps.empty())
    {
        if (OutError) *OutError = "Supported game spec produced no build steps";
        return std::nullopt;
    }
    std::string ValidationError;
    if (!ValidateAgentBuildPlan(Plan, nullptr, &ValidationError))
    {
        if (OutError) *OutError = ValidationError;
        return std::nullopt;
    }
    return Plan;
}

bool ValidateAgentBuildPlan(
    const FAgentBuildPlan& Plan,
    std::vector<std::string>* OutOrderedStepIds,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    if (Plan.Id.empty() || Plan.GameSpecId.empty() || Plan.Steps.empty())
    {
        if (OutError) *OutError = "Build plan requires ids and at least one step";
        return false;
    }
    std::unordered_map<std::string, std::size_t> IndexById;
    std::unordered_map<std::string, std::size_t> InDegree;
    std::unordered_map<std::string, std::vector<std::string>> Dependents;
    std::unordered_set<std::string> IdempotencyKeys;
    for (std::size_t Index = 0; Index < Plan.Steps.size(); ++Index)
    {
        const FAgentBuildPlanStep& Step = Plan.Steps[Index];
        if (Step.Id.empty() || Step.ProducerId.empty() || Step.Operation.empty()
            || Step.IdempotencyKey.empty() || Step.VerifierId.empty())
        {
            if (OutError) *OutError = "Build plan step has an incomplete contract";
            return false;
        }
        try
        {
            const FJson ParsedArguments = FJson::parse(Step.ArgumentsJson);
            (void)ParsedArguments;
        }
        catch (...)
        {
            if (OutError) *OutError = "Build plan step arguments are invalid JSON";
            return false;
        }
        if (!IndexById.emplace(Step.Id, Index).second
            || !IdempotencyKeys.insert(Step.IdempotencyKey).second)
        {
            if (OutError) *OutError = "Build plan step or idempotency key is duplicated";
            return false;
        }
        InDegree[Step.Id] = Step.Dependencies.size();
    }
    for (const FAgentBuildPlanStep& Step : Plan.Steps)
        for (const std::string& Dependency : Step.Dependencies)
        {
            if (!IndexById.contains(Dependency) || Dependency == Step.Id)
            {
                if (OutError) *OutError = "Build plan contains an unknown or self dependency";
                return false;
            }
            Dependents[Dependency].push_back(Step.Id);
        }
    std::priority_queue<std::string, std::vector<std::string>, std::greater<>> Ready;
    for (const auto& [Id, Degree] : InDegree) if (Degree == 0) Ready.push(Id);
    std::vector<std::string> Ordered;
    while (!Ready.empty())
    {
        std::string Id = Ready.top();
        Ready.pop();
        Ordered.push_back(Id);
        for (const std::string& Dependent : Dependents[Id])
            if (--InDegree[Dependent] == 0) Ready.push(Dependent);
    }
    if (Ordered.size() != Plan.Steps.size())
    {
        if (OutError) *OutError = "Build plan dependency graph contains a cycle";
        return false;
    }
    if (OutOrderedStepIds) *OutOrderedStepIds = std::move(Ordered);
    return true;
}

std::string HashAgentBuildPlan(const FAgentBuildPlan& Plan)
{
    return StableHash(PlanToJson(Plan).dump());
}

std::optional<FAgentBuildPlanDryRun> BuildAgentBuildPlanDryRun(
    const FAgentBuildPlan& Plan,
    std::string* OutError)
{
    FAgentBuildPlanDryRun Result;
    if (!ValidateAgentBuildPlan(Plan, &Result.OrderedStepIds, OutError))
        return std::nullopt;
    Result.PlanHash = HashAgentBuildPlan(Plan);
    std::set<std::string> Producers;
    std::set<std::string> SideEffects;
    std::set<std::string> Kinds;
    for (const FAgentBuildPlanStep& Step : Plan.Steps)
    {
        Producers.insert(Step.ProducerId);
        SideEffects.insert(Step.SideEffect);
        Kinds.insert(Step.ExpectedArtifactKinds.begin(),
            Step.ExpectedArtifactKinds.end());
    }
    Result.Producers.assign(Producers.begin(), Producers.end());
    Result.SideEffects.assign(SideEffects.begin(), SideEffects.end());
    Result.ExpectedArtifactKinds.assign(Kinds.begin(), Kinds.end());
    Result.ReportJson = FJson{{"plan_id", Plan.Id}, {"plan_hash", Result.PlanHash},
        {"ordered_steps", Result.OrderedStepIds}, {"producers", Result.Producers},
        {"side_effects", Result.SideEffects},
        {"expected_artifact_kinds", Result.ExpectedArtifactKinds}}.dump();
    return Result;
}

FAgentArtifactStore::FAgentArtifactStore(std::filesystem::path InRootDirectory)
    : RootDirectory(std::move(InRootDirectory))
{
}

std::optional<FAgentArtifact> FAgentArtifactStore::PublishText(
    std::string Kind,
    std::string Summary,
    std::string_view Content,
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (RootDirectory.empty() || Kind.empty())
    {
        if (OutError) *OutError = "Artifact root and kind are required";
        return std::nullopt;
    }
    const std::string Hash = StableHash(std::string(Kind) + '\n' + std::string(Content));
    const std::string FileName = Hash + ".artifact";
    const std::filesystem::path Directory = RootDirectory / "objects";
    const std::filesystem::path Destination = Directory / FileName;
    const std::filesystem::path Staging = Destination.string() + ".tmp";
    try
    {
        std::filesystem::create_directories(Directory);
        if (!std::filesystem::exists(Destination))
        {
            {
                std::ofstream Stream(Staging, std::ios::binary | std::ios::trunc);
                Stream.write(Content.data(),
                    static_cast<std::streamsize>(Content.size()));
                Stream.flush();
                if (!Stream) throw std::runtime_error("could not flush artifact");
            }
            std::string ReplaceError;
            if (!ReplaceFile(Staging, Destination, ReplaceError))
                throw std::runtime_error(ReplaceError);
        }
        FAgentArtifact Artifact;
        Artifact.Handle = "artifact:" + Hash;
        Artifact.Kind = std::move(Kind);
        Artifact.Summary = std::move(Summary);
        Artifact.RelativePath = (std::filesystem::path("objects") / FileName)
            .generic_string();
        Artifact.SizeBytes = Content.size();
        return Artifact;
    }
    catch (const std::exception& Exception)
    {
        std::error_code Ignore;
        std::filesystem::remove(Staging, Ignore);
        if (OutError) *OutError = Exception.what();
        return std::nullopt;
    }
}

std::optional<std::string> FAgentArtifactStore::ReadText(
    std::string_view Handle,
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    constexpr std::string_view Prefix = "artifact:";
    if (!Handle.starts_with(Prefix))
    {
        if (OutError) *OutError = "Artifact handle is invalid";
        return std::nullopt;
    }
    const std::string Hash(Handle.substr(Prefix.size()));
    if (Hash.size() != 16 || !std::all_of(Hash.begin(), Hash.end(),
        [](unsigned char Value) { return std::isxdigit(Value) != 0; }))
    {
        if (OutError) *OutError = "Artifact handle hash is invalid";
        return std::nullopt;
    }
    try
    {
        const std::filesystem::path Path = RootDirectory / "objects"
            / (Hash + ".artifact");
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream) throw std::runtime_error("artifact was not found");
        return std::string(std::istreambuf_iterator<char>(Stream), {});
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return std::nullopt;
    }
}

FAgentAssemblyCheckpointStore::FAgentAssemblyCheckpointStore(
    std::filesystem::path InFilePath)
    : FilePath(std::move(InFilePath))
{
}

bool FAgentAssemblyCheckpointStore::Save(
    const FAgentAssemblyCheckpoint& Checkpoint,
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (FilePath.empty() || Checkpoint.PlanHash.empty())
    {
        if (OutError) *OutError = "Checkpoint path and plan hash are required";
        return false;
    }
    try
    {
        FJson Artifacts = FJson::array();
        for (const FAgentArtifact& Artifact : Checkpoint.Artifacts)
            Artifacts.push_back(ArtifactToJson(Artifact));
        FJson Root = {{"format_version", Checkpoint.FormatVersion},
            {"plan_hash", Checkpoint.PlanHash},
            {"completed_step_ids", Checkpoint.CompletedStepIds},
            {"completed_idempotency_keys", Checkpoint.CompletedIdempotencyKeys},
            {"artifacts", Artifacts},
            {"summary", FJson::parse(Checkpoint.SummaryJson)},
            {"counters", {{"steps", Checkpoint.Counters.Steps},
                {"tool_calls", Checkpoint.Counters.ToolCalls},
                {"context_messages", Checkpoint.Counters.ContextMessages},
                {"trimmed_context_messages", Checkpoint.Counters.TrimmedContextMessages}}}};
        std::filesystem::create_directories(FilePath.parent_path());
        const std::filesystem::path Staging = FilePath.string() + ".tmp";
        {
            std::ofstream Stream(Staging, std::ios::binary | std::ios::trunc);
            Stream << Root.dump(2) << '\n';
            Stream.flush();
            if (!Stream) throw std::runtime_error("could not flush checkpoint");
        }
        std::string ReplaceError;
        if (!ReplaceFile(Staging, FilePath, ReplaceError))
            throw std::runtime_error(ReplaceError);
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

std::optional<FAgentAssemblyCheckpoint> FAgentAssemblyCheckpointStore::Load(
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (!std::filesystem::exists(FilePath)) return std::nullopt;
    try
    {
        std::ifstream Stream(FilePath, std::ios::binary);
        FJson Root;
        Stream >> Root;
        if (Root.value("format_version", 0) != 1)
            throw std::runtime_error("unsupported checkpoint format");
        FAgentAssemblyCheckpoint Result;
        Result.PlanHash = Root.value("plan_hash", "");
        Result.CompletedStepIds = Root.value(
            "completed_step_ids", std::vector<std::string>{});
        Result.CompletedIdempotencyKeys = Root.value(
            "completed_idempotency_keys", std::vector<std::string>{});
        Result.SummaryJson = Root.value("summary", FJson::object()).dump();
        for (const FJson& Entry : Root.value("artifacts", FJson::array()))
            Result.Artifacts.push_back(ArtifactFromJson(Entry));
        const FJson Counters = Root.value("counters", FJson::object());
        Result.Counters.Steps = Counters.value("steps", 0U);
        Result.Counters.ToolCalls = Counters.value("tool_calls", 0U);
        Result.Counters.ContextMessages = Counters.value("context_messages", 0U);
        Result.Counters.TrimmedContextMessages = Counters.value(
            "trimmed_context_messages", 0U);
        if (Result.PlanHash.empty()) throw std::runtime_error("checkpoint has no plan hash");
        return Result;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return std::nullopt;
    }
}

FAgentBuildPlanExecutionResult ExecuteAgentBuildPlan(
    const FAgentBuildPlan& Plan,
    std::string_view ApprovedPlanHash,
    const std::filesystem::path& StagingRoot,
    const std::unordered_map<std::string, IAgentArtifactProducer*>& Producers,
    IAgentBuildPlanTransaction& Transaction,
    const FAgentAssemblyCheckpointStore& CheckpointStore,
    const FAgentAssemblyCheckpoint* ResumeFrom)
{
    FAgentBuildPlanExecutionResult Result;
    Result.PlanHash = HashAgentBuildPlan(Plan);
    std::vector<std::string> Ordered;
    if (!ValidateAgentBuildPlan(Plan, &Ordered, &Result.Error)) return Result;
    if (ApprovedPlanHash != Result.PlanHash)
    {
        Result.Error = "Approved PlanHash does not match the current build plan";
        return Result;
    }
    if (ResumeFrom)
    {
        if (ResumeFrom->PlanHash != Result.PlanHash)
        {
            Result.Error = "Checkpoint PlanHash does not match the current build plan";
            return Result;
        }
        Result.Checkpoint = *ResumeFrom;
        Result.bResumed = !ResumeFrom->CompletedStepIds.empty();
    }
    else Result.Checkpoint.PlanHash = Result.PlanHash;
    std::unordered_set<std::string> CompletedKeys(
        Result.Checkpoint.CompletedIdempotencyKeys.begin(),
        Result.Checkpoint.CompletedIdempotencyKeys.end());
    std::string TransactionError;
    if (!Transaction.Begin(Result.PlanHash, TransactionError))
    {
        Result.Error = "Could not begin build plan transaction: " + TransactionError;
        return Result;
    }
    const auto Fail = [&](std::string StepId, std::string Error)
    {
        std::string RollbackError;
        Transaction.Rollback(RollbackError);
        Result.FailedStepId = std::move(StepId);
        Result.Error = std::move(Error);
        if (!RollbackError.empty()) Result.Error += "; rollback: " + RollbackError;
        return Result;
    };
    std::error_code DirectoryError;
    std::filesystem::create_directories(StagingRoot / Result.PlanHash, DirectoryError);
    if (DirectoryError)
        return Fail({}, "Could not create plan staging directory: "
            + DirectoryError.message());
    for (const std::string& StepId : Ordered)
    {
        const FAgentBuildPlanStep& Step = *std::find_if(
            Plan.Steps.begin(), Plan.Steps.end(),
            [&StepId](const auto& Entry) { return Entry.Id == StepId; });
        if (CompletedKeys.contains(Step.IdempotencyKey)) continue;
        const auto Producer = Producers.find(Step.ProducerId);
        if (Producer == Producers.end() || Producer->second == nullptr)
            return Fail(Step.Id, "Build plan producer is unavailable: "
                + Step.ProducerId);
        std::vector<FAgentArtifact> Artifacts;
        std::string ProduceError;
        if (!Producer->second->Produce(Step,
                StagingRoot / Result.PlanHash / Step.Id, Artifacts, ProduceError))
            return Fail(Step.Id, "Producer failed: " + ProduceError);
        Result.Checkpoint.CompletedStepIds.push_back(Step.Id);
        Result.Checkpoint.CompletedIdempotencyKeys.push_back(Step.IdempotencyKey);
        Result.Checkpoint.Artifacts.insert(Result.Checkpoint.Artifacts.end(),
            Artifacts.begin(), Artifacts.end());
        ++Result.Checkpoint.Counters.Steps;
        CompletedKeys.insert(Step.IdempotencyKey);
        if (!CheckpointStore.Save(Result.Checkpoint, &ProduceError))
            return Fail(Step.Id, "Could not persist plan checkpoint: " + ProduceError);
    }
    if (!Transaction.Commit(TransactionError))
        return Fail({}, "Could not commit build plan transaction: " + TransactionError);
    Result.bSucceeded = true;
    return Result;
}

std::string_view ToString(EAgentRequirementSupport Support)
{
    switch (Support)
    {
    case EAgentRequirementSupport::Supported: return "Supported";
    case EAgentRequirementSupport::MissingDependency: return "MissingDependency";
    case EAgentRequirementSupport::Unsupported: return "Unsupported";
    }
    return "Unsupported";
}
}
