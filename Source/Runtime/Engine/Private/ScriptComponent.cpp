#include "Pico/Engine/ScriptComponent.h"

#include "Pico/Core/Paths.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Graph/GraphAsset.h"
#include "Pico/Graph/GraphCompiler.h"
#include "Pico/Graph/ScriptRuntimeExtensions.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PScriptComponent)

bool PScriptComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_ASSET_PROPERTY(Properties, GraphAsset, Graph);
    PICO_ADD_PROPERTY(Properties, bExecuteOnBeginPlay);
    PICO_ADD_PROPERTY(Properties, MaxInstructions);
    PICO_ADD_PROPERTY(Properties, MaxLoopIterations);
    PICO_ADD_PROPERTY(Properties, MaxCallDepth);
    FPropertyMetadata RuntimeMetadata;
    RuntimeMetadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    RuntimeMetadata.DisplayName = "Execution State";
    RuntimeMetadata.EnumOptions = {
        {0, "Idle"}, {1, "Running"}, {2, "Suspended"},
        {3, "Succeeded"}, {4, "Failed"}};
    PICO_ADD_PROPERTY_METADATA(Properties, ExecutionStateValue, RuntimeMetadata);
    RuntimeMetadata.DisplayName = "Active Latent Action";
    RuntimeMetadata.EnumOptions = {
        {0, "None"}, {1, "Delay"}, {2, "Wait Gameplay Event"},
        {3, "Play Montage And Wait"}};
    PICO_ADD_PROPERTY_METADATA(Properties, ActiveLatentActionValue, RuntimeMetadata);
    RuntimeMetadata.DisplayName = "Last Instructions Executed";
    RuntimeMetadata.EnumOptions.clear();
    PICO_ADD_PROPERTY_METADATA(Properties, LastInstructionsExecuted, RuntimeMetadata);

    std::vector<PFunction> Functions;
    PICO_ADD_FUNCTION(Functions, ExecuteBeginPlayGraph, EFunctionFlags::Callable);
    return Class.AddProperties(std::move(Properties))
        && Class.AddFunctions(std::move(Functions));
}

PScriptComponent::PScriptComponent(const FObjectConstructionParams& Params)
    : PActorComponent(Params)
{
    PrimaryComponentTick.SetCanEverTick(true);
}

const FAssetPath& PScriptComponent::GetGraphAsset() const { return GraphAsset; }
void PScriptComponent::SetGraphAsset(const FAssetPath& AssetPath) { GraphAsset = AssetPath; }
bool PScriptComponent::GetExecuteOnBeginPlay() const { return bExecuteOnBeginPlay; }
void PScriptComponent::SetExecuteOnBeginPlay(bool bValue) { bExecuteOnBeginPlay = bValue; }
const FScriptExecutionReport& PScriptComponent::GetLastExecutionReport() const
{
    return LastExecutionReport;
}
EScriptComponentExecutionState PScriptComponent::GetExecutionState() const
{
    return static_cast<EScriptComponentExecutionState>(ExecutionStateValue);
}
EScriptLatentAction PScriptComponent::GetActiveLatentAction() const
{
    return static_cast<EScriptLatentAction>(ActiveLatentActionValue);
}
float PScriptComponent::GetLatentRemainingSeconds() const
{
    return LatentRemainingSeconds;
}
const std::vector<FScriptScreenMessage>& PScriptComponent::GetScreenMessages() const
{
    return ScreenMessages;
}

void PScriptComponent::OnRegister()
{
    PActorComponent::OnRegister();
    if (bExecuteOnBeginPlay && GetOwner() != nullptr && GetOwner()->HasBegunPlay())
        ExecuteBeginPlayGraph();
}

void PScriptComponent::OnUnregister()
{
    CancelExecution();
    PActorComponent::OnUnregister();
}

bool PScriptComponent::CancelExecution()
{
    const bool bWasActive = GetExecutionState() == EScriptComponentExecutionState::Running
        || GetExecutionState() == EScriptComponentExecutionState::Suspended;
    ++ExecutionGeneration;
    if (CancelPendingAction) CancelPendingAction();
    CancelPendingAction = {};
    ActiveBytecode = {};
    PendingContinuation = UINT32_MAX;
    PendingAlternateContinuation = UINT32_MAX;
    LatentRemainingSeconds = 0.0f;
    ActiveLatentActionValue = static_cast<int32>(EScriptLatentAction::None);
    if (bWasActive) ExecutionStateValue = static_cast<int32>(EScriptComponentExecutionState::Idle);
    return bWasActive;
}

void PScriptComponent::TickComponent(float DeltaSeconds)
{
    PActorComponent::TickComponent(DeltaSeconds);
    for (FScriptScreenMessage& Message : ScreenMessages)
        Message.RemainingSeconds -= DeltaSeconds;
    std::erase_if(ScreenMessages,
        [](const FScriptScreenMessage& Message)
        {
            return Message.RemainingSeconds <= 0.0f;
        });
    if (GetExecutionState() != EScriptComponentExecutionState::Suspended
        || GetActiveLatentAction() != EScriptLatentAction::Delay)
        return;
    LatentRemainingSeconds = std::max(0.0f, LatentRemainingSeconds - DeltaSeconds);
    if (LatentRemainingSeconds <= 0.0f)
        CompleteLatentAction(ExecutionGeneration, true, "Delay completed");
}

bool PScriptComponent::ExecuteBeginPlayGraph()
{
    const FScriptExecutionReport Report = ExecuteEvent("BeginPlay");
    return Report.Succeeded() || Report.IsSuspended();
}

FScriptExecutionReport PScriptComponent::ExecuteEvent(std::string_view EventName)
{
    if (!GraphAsset.IsValid())
    {
        LastExecutionReport = {
            EScriptExecutionResult::InvalidProgram, "ScriptComponent has no valid GraphAsset"};
        return LastExecutionReport;
    }
    constexpr std::string_view Prefix = "/Game/";
    const std::string_view VirtualPath = GraphAsset.ToString();
    if (!VirtualPath.starts_with(Prefix))
    {
        LastExecutionReport = {
            EScriptExecutionResult::InvalidProgram, "GraphAsset is outside /Game"};
        return LastExecutionReport;
    }
    const std::filesystem::path File = FPaths::GetProjectContentDir()
        / std::filesystem::path(std::string(VirtualPath.substr(Prefix.size())));
    const std::filesystem::path CookedFile = File.string() + ".pgrb";
    FPicoGraphBytecode Bytecode;
    std::string CookedError;
    if (std::filesystem::is_regular_file(CookedFile))
    {
        if (LoadGraphBytecodeFromFile(CookedFile, Bytecode, &CookedError))
            return ExecuteBytecode(Bytecode, EventName);
        LastExecutionReport = {
            EScriptExecutionResult::InvalidProgram,
            "Could not load cooked Graph bytecode: " + CookedError};
        return LastExecutionReport;
    }

    FPicoGraphAsset Graph;
    EGraphAssetError AssetError = EGraphAssetError::None;
    if (!LoadGraphAssetFromFile(File, Graph, &AssetError))
    {
        LastExecutionReport = {
            EScriptExecutionResult::InvalidProgram,
            "Could not load GraphAsset: " + std::string(ToString(AssetError))};
        return LastExecutionReport;
    }
    FGraphCompileResult Compile = CompileGraph(Graph);
    if (!Compile.bSucceeded)
    {
        LastExecutionReport = {
            EScriptExecutionResult::InvalidProgram, "GraphAsset did not compile"};
        return LastExecutionReport;
    }
    return ExecuteBytecode(Compile.Bytecode, EventName);
}

FScriptExecutionReport PScriptComponent::ExecuteBytecode(
    const FPicoGraphBytecode& Bytecode,
    std::string_view EventName)
{
    CancelExecution();
    ScreenMessages.clear();
    ++ExecutionGeneration;
    ActiveBytecode = Bytecode;
    FScriptExecutionContext Context;
    Context.Self = GetOwner();
    Context.EntryEvent = EventName;
    Context.Limits.MaxInstructions = static_cast<std::size_t>(std::max(1, MaxInstructions));
    Context.Limits.MaxLoopIterations = static_cast<std::size_t>(std::max(1, MaxLoopIterations));
    Context.Limits.MaxCallDepth = static_cast<std::size_t>(std::max(1, MaxCallDepth));
    Context.ActivateAbility = [Self = Context.Self](int32 Handle)
    {
        return ActivateScriptAbility(Self, Handle);
    };
    Context.PrintString = [this](std::string Message, float DurationSeconds)
    {
        AddScreenMessage(std::move(Message), DurationSeconds);
    };
    ExecutionStateValue = static_cast<int32>(EScriptComponentExecutionState::Running);
    ApplyExecutionReport(FPicoScriptVM().Execute(Bytecode, Context));
    return LastExecutionReport;
}

FScriptExecutionReport PScriptComponent::ExecuteContinuation(std::uint32_t Instruction)
{
    FScriptExecutionContext Context;
    Context.Self = GetOwner();
    Context.StartInstruction = Instruction;
    Context.Limits.MaxInstructions = static_cast<std::size_t>(std::max(1, MaxInstructions));
    Context.Limits.MaxLoopIterations = static_cast<std::size_t>(std::max(1, MaxLoopIterations));
    Context.Limits.MaxCallDepth = static_cast<std::size_t>(std::max(1, MaxCallDepth));
    Context.ActivateAbility = [Self = Context.Self](int32 Handle)
    {
        return ActivateScriptAbility(Self, Handle);
    };
    Context.PrintString = [this](std::string Message, float DurationSeconds)
    {
        AddScreenMessage(std::move(Message), DurationSeconds);
    };
    ExecutionStateValue = static_cast<int32>(EScriptComponentExecutionState::Running);
    ApplyExecutionReport(FPicoScriptVM().Execute(ActiveBytecode, Context));
    return LastExecutionReport;
}

void PScriptComponent::AddScreenMessage(std::string Message, float DurationSeconds)
{
    if (Message.empty() || DurationSeconds <= 0.0f) return;
    constexpr std::size_t MaxMessages = 8;
    if (ScreenMessages.size() == MaxMessages) ScreenMessages.erase(ScreenMessages.begin());
    ScreenMessages.push_back({std::move(Message), DurationSeconds});
}

void PScriptComponent::ApplyExecutionReport(FScriptExecutionReport Report)
{
    LastExecutionReport = std::move(Report);
    LastInstructionsExecuted = static_cast<int32>(std::min<std::size_t>(
        LastExecutionReport.InstructionsExecuted,
        static_cast<std::size_t>(std::numeric_limits<int32>::max())));
    if (LastExecutionReport.IsSuspended())
    {
        ExecutionStateValue = static_cast<int32>(EScriptComponentExecutionState::Suspended);
        ActiveLatentActionValue = static_cast<int32>(LastExecutionReport.LatentAction);
        PendingContinuation = LastExecutionReport.ContinuationInstruction;
        PendingAlternateContinuation = LastExecutionReport.AlternateContinuationInstruction;
        LatentRemainingSeconds = LastExecutionReport.LatentAction
                == EScriptLatentAction::Delay
            ? LastExecutionReport.LatentSeconds : 0.0f;
        if (LastExecutionReport.LatentAction == EScriptLatentAction::Delay) return;

        const FObjectHandle Handle = GetHandle();
        const uint64 Generation = ExecutionGeneration;
        FScriptLatentCancel Cancel;
        const bool bStarted = StartScriptLatentAction(
            GetOwner(), LastExecutionReport,
            [Handle, Generation](bool bSucceeded, std::string Message)
            {
                PObject* Object = ResolveObject(Handle);
                if (Object != nullptr && Object->IsA(PScriptComponent::StaticClass()))
                    static_cast<PScriptComponent*>(Object)->CompleteLatentAction(
                        Generation, bSucceeded, std::move(Message));
            }, Cancel);
        if (bStarted)
        {
            CancelPendingAction = std::move(Cancel);
            return;
        }
        LastExecutionReport.Result = EScriptExecutionResult::ReflectionError;
        LastExecutionReport.Message = "No runtime handler accepted the latent Graph action";
    }
    ActiveLatentActionValue = static_cast<int32>(EScriptLatentAction::None);
    ExecutionStateValue = static_cast<int32>(LastExecutionReport.Succeeded()
        ? EScriptComponentExecutionState::Succeeded
        : EScriptComponentExecutionState::Failed);
    ActiveBytecode = {};
}

void PScriptComponent::CompleteLatentAction(
    uint64 Generation,
    bool bSucceeded,
    std::string Message)
{
    if (Generation != ExecutionGeneration
        || GetExecutionState() != EScriptComponentExecutionState::Suspended)
        return;
    CancelPendingAction = {};
    LatentRemainingSeconds = 0.0f;
    ActiveLatentActionValue = static_cast<int32>(EScriptLatentAction::None);
    const std::uint32_t Continuation = bSucceeded
        ? PendingContinuation : PendingAlternateContinuation;
    PendingContinuation = UINT32_MAX;
    PendingAlternateContinuation = UINT32_MAX;
    if (Continuation == UINT32_MAX)
    {
        LastExecutionReport.Result = bSucceeded
            ? EScriptExecutionResult::Success : EScriptExecutionResult::ReflectionError;
        LastExecutionReport.Message = Message.empty()
            ? (bSucceeded ? "Latent action completed" : "Latent action failed")
            : std::move(Message);
        ExecutionStateValue = static_cast<int32>(bSucceeded
            ? EScriptComponentExecutionState::Succeeded
            : EScriptComponentExecutionState::Failed);
        ActiveBytecode = {};
        return;
    }
    ExecuteContinuation(Continuation);
}
}
