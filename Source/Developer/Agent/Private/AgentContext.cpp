#include "Pico/Agent/AgentContext.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <iomanip>
#include <sstream>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::uint64_t MeasureMessageBytes(const FAgentMessage& Message)
{
    std::uint64_t Bytes = Message.Content.size() + Message.ToolCallId.size();
    for (const FAgentToolCall& Call : Message.ToolCalls)
        Bytes += Call.Id.size() + Call.Name.size() + Call.ArgumentsJson.size();
    return Bytes;
}

bool IsEmptyJson(std::string_view Value, std::string_view EmptyValue)
{
    return Value.empty() || Value == EmptyValue;
}

bool IncludeWithinBudget(
    std::string& Output,
    std::string Input,
    std::string_view EmptyValue,
    std::uint64_t Limit,
    std::uint64_t& Used,
    std::uint64_t& IncludedBytes,
    std::uint64_t& DroppedBytes)
{
    if (IsEmptyJson(Input, EmptyValue))
    {
        Output = std::string(EmptyValue);
        return true;
    }
    const std::uint64_t Size = Input.size();
    if (Size > Limit - std::min(Limit, Used))
    {
        Output = std::string(EmptyValue);
        DroppedBytes += Size;
        return false;
    }
    Output = std::move(Input);
    Used += Size;
    IncludedBytes += Size;
    return true;
}

std::string CanonicalJson(std::string_view Json)
{
    try { return FJson::parse(Json).dump(); }
    catch (...) { return std::string(Json); }
}

std::string StableHash(std::string_view Value)
{
    std::uint64_t Hash = 1469598103934665603ULL;
    for (const unsigned char Character : Value)
    {
        Hash ^= Character;
        Hash *= 1099511628211ULL;
    }
    std::ostringstream Stream;
    Stream << std::hex << std::setfill('0') << std::setw(16) << Hash;
    return Stream.str();
}
}

std::string SerializeAgentTaskState(const FAgentTaskState& State)
{
    FJson CriterionEvidence = FJson::array();
    for (const FAgentCriterionEvidence& Binding : State.CriterionEvidence)
    {
        CriterionEvidence.push_back({{"criterion", Binding.Criterion},
            {"evidence_refs", Binding.EvidenceRefs},
            {"satisfied", Binding.bSatisfied}});
    }
    return FJson {{"version", 3},
        {"goal", State.Goal},
        {"success_criteria", State.SuccessCriteria},
        {"constraints", State.Constraints},
        {"current_step", State.CurrentStep},
        {"remaining_steps", State.RemainingSteps},
        {"evidence_refs", State.EvidenceRefs},
        {"criterion_evidence", std::move(CriterionEvidence)},
        {"open_questions", State.OpenQuestions},
        {"mutation_readback_pending", State.bMutationReadbackPending},
        {"pending_mutation_tool", State.PendingMutationTool},
        {"observation_count", State.ObservationCount},
        {"revision", State.Revision}}.dump();
}

bool DeserializeAgentTaskState(
    std::string_view Json,
    FAgentTaskState& OutState,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    try
    {
        const FJson Value = FJson::parse(Json);
        const int Version = Value.value("version", 0);
        if (!Value.is_object()
            || (Version != 1 && Version != 2 && Version != 3))
        {
            if (OutError) *OutError = "Unsupported Agent Task State version";
            return false;
        }
        FAgentTaskState State;
        State.Goal = Value.value("goal", "");
        State.SuccessCriteria = Value.value(
            "success_criteria", std::vector<std::string>{});
        State.Constraints = Value.value(
            "constraints", std::vector<std::string>{});
        State.CurrentStep = Value.value("current_step", "");
        State.RemainingSteps = Value.value(
            "remaining_steps", std::vector<std::string>{});
        State.EvidenceRefs = Value.value(
            "evidence_refs", std::vector<std::string>{});
        if (Version >= 2)
        {
            for (const FJson& Item : Value.value(
                    "criterion_evidence", FJson::array()))
            {
                State.CriterionEvidence.push_back({Item.value("criterion", ""),
                    Item.value("evidence_refs", std::vector<std::string>{}),
                    Item.value("satisfied", false)});
            }
        }
        if (State.CriterionEvidence.empty())
        {
            for (const std::string& Criterion : State.SuccessCriteria)
                State.CriterionEvidence.push_back({Criterion, {}, false});
        }
        State.OpenQuestions = Value.value(
            "open_questions", std::vector<std::string>{});
        if (Version >= 3)
        {
            State.bMutationReadbackPending = Value.value(
                "mutation_readback_pending", false);
            State.PendingMutationTool = Value.value(
                "pending_mutation_tool", "");
        }
        State.ObservationCount = Value.value(
            "observation_count", std::uint64_t {0});
        State.Revision = Value.value("revision", std::uint64_t {0});
        OutState = std::move(State);
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

std::string BuildAgentActionFingerprint(const FAgentToolCall& Call)
{
    const std::string Canonical = Call.Name + "\n" + CanonicalJson(
        Call.ArgumentsJson);
    return "action:" + StableHash(Canonical);
}

FAgentObservation BuildAgentObservation(
    const FAgentToolCall& Call,
    const FAgentToolResult& Result,
    bool bReadOnly,
    bool bVerified)
{
    FAgentObservation Observation;
    Observation.CallId = Call.Id;
    Observation.ToolName = Call.Name;
    Observation.ActionFingerprint = BuildAgentActionFingerprint(Call);
    Observation.FactsJson = Result.FactsJson;
    Observation.bSucceeded = Result.bSucceeded;
    Observation.bVerified = bVerified;
    Observation.bReadOnly = bReadOnly;
    Observation.bReused = Result.bReused;
    Observation.Artifacts = Result.Artifacts;
    Observation.Diagnostics = Result.Diagnostics;
    Observation.StateChanges = Result.StateChanges;
    Observation.RevisionChanges = Result.RevisionChanges;
    const bool bRevisionAdvanced = std::any_of(
        Result.RevisionChanges.begin(), Result.RevisionChanges.end(),
        [](const FAgentRevisionChange& Change)
        {
            return Change.After > Change.Before;
        });
    const bool bHasEvidence = (!Result.FactsJson.empty()
            && Result.FactsJson != "{}" && Result.FactsJson != "null")
        || !Result.Artifacts.empty() || !Result.StateChanges.empty()
        || !Result.RevisionChanges.empty() || !Result.Diagnostics.empty();
    if (Observation.bVerified && bHasEvidence)
        Observation.EvidenceRef = "observation:" + Call.Id;
    Observation.bMadeProgress = Observation.bVerified && !Result.bReused
        && (bHasEvidence || !bReadOnly || bRevisionAdvanced);
    return Observation;
}

std::string SerializeAgentObservation(const FAgentObservation& Observation)
{
    FJson Facts;
    try { Facts = FJson::parse(Observation.FactsJson); }
    catch (...) { Facts = Observation.FactsJson; }
    FJson Artifacts = FJson::array();
    for (const FAgentArtifact& Artifact : Observation.Artifacts)
        Artifacts.push_back({{"handle", Artifact.Handle}, {"kind", Artifact.Kind},
            {"summary", Artifact.Summary}});
    FJson Revisions = FJson::array();
    for (const FAgentRevisionChange& Change : Observation.RevisionChanges)
        Revisions.push_back({{"domain", Change.Domain}, {"before", Change.Before},
            {"after", Change.After}});
    return FJson {{"call_id", Observation.CallId},
        {"tool", Observation.ToolName},
        {"action_fingerprint", Observation.ActionFingerprint},
        {"evidence_ref", Observation.EvidenceRef},
        {"succeeded", Observation.bSucceeded},
        {"verified", Observation.bVerified},
        {"read_only", Observation.bReadOnly},
        {"reused", Observation.bReused},
        {"made_progress", Observation.bMadeProgress},
        {"facts", std::move(Facts)},
        {"artifacts", std::move(Artifacts)},
        {"state_changes", Observation.StateChanges},
        {"revision_changes", std::move(Revisions)}}.dump();
}

void BindAgentObservationEvidence(
    const FAgentObservation& Observation,
    FAgentTaskState& InOutState)
{
    if (!Observation.bVerified || Observation.EvidenceRef.empty()) return;
    if (std::find(InOutState.EvidenceRefs.begin(), InOutState.EvidenceRefs.end(),
            Observation.EvidenceRef) == InOutState.EvidenceRefs.end())
        InOutState.EvidenceRefs.push_back(Observation.EvidenceRef);
    if (InOutState.EvidenceRefs.size() > 16)
        InOutState.EvidenceRefs.erase(InOutState.EvidenceRefs.begin());

    if (InOutState.CriterionEvidence.empty())
    {
        for (const std::string& Criterion : InOutState.SuccessCriteria)
            InOutState.CriterionEvidence.push_back({Criterion, {}, false});
    }
    for (FAgentCriterionEvidence& Binding : InOutState.CriterionEvidence)
    {
        if (std::find(Binding.EvidenceRefs.begin(), Binding.EvidenceRefs.end(),
                Observation.EvidenceRef) == Binding.EvidenceRefs.end())
            Binding.EvidenceRefs.push_back(Observation.EvidenceRef);
        if (Binding.EvidenceRefs.size() > 16)
            Binding.EvidenceRefs.erase(Binding.EvidenceRefs.begin());
        Binding.bSatisfied = !Binding.EvidenceRefs.empty();
    }
    ++InOutState.Revision;
}

bool HasAgentCompletionEvidence(const FAgentTaskState& State)
{
    if (State.SuccessCriteria.empty()) return !State.EvidenceRefs.empty();
    if (State.CriterionEvidence.size() < State.SuccessCriteria.size()) return false;
    return std::all_of(State.CriterionEvidence.begin(),
        State.CriterionEvidence.end(), [](const FAgentCriterionEvidence& Binding)
        {
            return Binding.bSatisfied && !Binding.EvidenceRefs.empty();
        });
}

std::vector<FAgentMessage> BuildBoundedAgentMessageHistory(
    std::vector<FAgentMessage> History,
    std::size_t MaxMessages,
    std::uint64_t MaxBytes,
    std::size_t* OutTrimmedMessages)
{
    const std::size_t OriginalCount = History.size();
    std::uint64_t UsedBytes = 0;
    std::size_t FirstKept = History.size();
    while (FirstKept > 0 && History.size() - FirstKept < MaxMessages)
    {
        const std::uint64_t MessageBytes = MeasureMessageBytes(
            History[FirstKept - 1]);
        if (MessageBytes > MaxBytes - std::min(MaxBytes, UsedBytes)) break;
        UsedBytes += MessageBytes;
        --FirstKept;
    }
    if (FirstKept > 0)
        History.erase(History.begin(), History.begin() + FirstKept);
    while (!History.empty() && History.front().Role == EAgentRole::Tool)
        History.erase(History.begin());
    if (OutTrimmedMessages)
        *OutTrimmedMessages = OriginalCount - History.size();
    return History;
}

FAgentAssembledContext FAgentContextAssembler::Assemble(
    FAgentContextAssemblyInput Input)
{
    const auto StartedAt = std::chrono::steady_clock::now();
    FAgentAssembledContext Result;
    const std::uint64_t ExternalLimit = Input.MaxBytes / 2;
    std::uint64_t ExternalBytes = 0;

    IncludeWithinBudget(Result.TaskStateJson, std::move(Input.TaskStateJson),
        "{}", ExternalLimit, ExternalBytes, Result.Metrics.TaskStateBytes,
        Result.Metrics.DroppedBytes);
    IncludeWithinBudget(Result.SkillContextJson, std::move(Input.SkillContextJson),
        "[]", ExternalLimit, ExternalBytes, Result.Metrics.InstructionBytes,
        Result.Metrics.DroppedBytes);
    IncludeWithinBudget(Result.ObservationContextJson,
        std::move(Input.ObservationContextJson), "{}", ExternalLimit,
        ExternalBytes, Result.Metrics.ObservationBytes,
        Result.Metrics.DroppedBytes);
    IncludeWithinBudget(Result.KnowledgeContextJson,
        std::move(Input.KnowledgeContextJson), "{}", ExternalLimit,
        ExternalBytes, Result.Metrics.MemoryBytes,
        Result.Metrics.DroppedBytes);

    const std::uint64_t MessageBudget = Input.MaxBytes
        - std::min(Input.MaxBytes, ExternalBytes);
    Result.Messages = BuildBoundedAgentMessageHistory(
        std::move(Input.Messages), Input.MaxMessages, MessageBudget,
        &Result.TrimmedMessages);
    for (const FAgentMessage& Message : Result.Messages)
        Result.Metrics.ConversationBytes += MeasureMessageBytes(Message);
    Result.Metrics.TotalBytes = ExternalBytes
        + Result.Metrics.ConversationBytes;
    Result.Metrics.AssemblyCount = 1;
    const auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - StartedAt).count();
    Result.Metrics.TotalAssemblyMicroseconds = Duration > 0
        ? static_cast<std::uint64_t>(Duration) : 0;
    Result.Metrics.MaxAssemblyMicroseconds =
        Result.Metrics.TotalAssemblyMicroseconds;
    return Result;
}
}
