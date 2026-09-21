#pragma once

#include "Pico/Agent/AgentTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
struct FAgentCriterionEvidence
{
    std::string Criterion;
    std::vector<std::string> EvidenceRefs;
    bool bSatisfied = false;
};

struct FAgentTaskState
{
    std::string Goal;
    std::vector<std::string> SuccessCriteria;
    std::vector<std::string> Constraints;
    std::string CurrentStep;
    std::vector<std::string> RemainingSteps;
    std::vector<std::string> EvidenceRefs;
    std::vector<FAgentCriterionEvidence> CriterionEvidence;
    std::vector<std::string> OpenQuestions;
    std::uint64_t ObservationCount = 0;
    std::uint64_t Revision = 0;
};

std::string SerializeAgentTaskState(const FAgentTaskState& State);
bool DeserializeAgentTaskState(
    std::string_view Json,
    FAgentTaskState& OutState,
    std::string* OutError = nullptr);

struct FAgentContextFeatureFlags
{
    bool bTaskState = true;
    bool bContextAssembler = true;
    bool bContextMetrics = true;
    bool bObservationMapping = true;
    bool bEvidenceCompletionGate = true;
    bool bActionOscillationGuard = true;
    bool bConditionalReflection = true;
};

struct FAgentObservation
{
    std::string CallId;
    std::string ToolName;
    std::string ActionFingerprint;
    std::string EvidenceRef;
    std::string FactsJson = "{}";
    bool bSucceeded = false;
    bool bVerified = false;
    bool bReadOnly = false;
    bool bReused = false;
    bool bMadeProgress = false;
    std::vector<FAgentArtifact> Artifacts;
    std::vector<FAgentDiagnostic> Diagnostics;
    std::vector<std::string> StateChanges;
    std::vector<FAgentRevisionChange> RevisionChanges;
};

std::string BuildAgentActionFingerprint(const FAgentToolCall& Call);
FAgentObservation BuildAgentObservation(
    const FAgentToolCall& Call,
    const FAgentToolResult& Result,
    bool bReadOnly,
    bool bVerified);
std::string SerializeAgentObservation(const FAgentObservation& Observation);
void BindAgentObservationEvidence(
    const FAgentObservation& Observation,
    FAgentTaskState& InOutState);
bool HasAgentCompletionEvidence(const FAgentTaskState& State);

struct FAgentContextAssemblyInput
{
    std::vector<FAgentMessage> Messages;
    std::string TaskStateJson = "{}";
    std::string ObservationContextJson = "{}";
    std::string KnowledgeContextJson = "{}";
    std::string SkillContextJson = "[]";
    std::size_t MaxMessages = 48;
    std::uint64_t MaxBytes = 256 * 1024;
};

struct FAgentAssembledContext
{
    std::vector<FAgentMessage> Messages;
    std::string TaskStateJson = "{}";
    std::string ObservationContextJson = "{}";
    std::string KnowledgeContextJson = "{}";
    std::string SkillContextJson = "[]";
    std::size_t TrimmedMessages = 0;
    FAgentContextMetrics Metrics;
};

std::vector<FAgentMessage> BuildBoundedAgentMessageHistory(
    std::vector<FAgentMessage> History,
    std::size_t MaxMessages,
    std::uint64_t MaxBytes,
    std::size_t* OutTrimmedMessages = nullptr);

class FAgentContextAssembler
{
public:
    static FAgentAssembledContext Assemble(
        FAgentContextAssemblyInput Input);
};
}
