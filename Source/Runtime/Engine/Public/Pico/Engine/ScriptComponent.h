#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Graph/ScriptVM.h"

#include <functional>
#include <string>
#include <vector>

namespace Pico
{
enum class EScriptComponentExecutionState : int32
{
    Idle,
    Running,
    Suspended,
    Succeeded,
    Failed
};

struct FScriptScreenMessage
{
    std::string Text;
    float RemainingSeconds = 0.0f;
};

class PScriptComponent : public PActorComponent
{
    PICO_DECLARE_CLASS(PScriptComponent, PActorComponent)

public:
    const FAssetPath& GetGraphAsset() const;
    void SetGraphAsset(const FAssetPath& AssetPath);
    bool GetExecuteOnBeginPlay() const;
    void SetExecuteOnBeginPlay(bool bValue);
    bool ExecuteBeginPlayGraph();
    FScriptExecutionReport ExecuteEvent(std::string_view EventName);
    FScriptExecutionReport ExecuteBytecode(
        const FPicoGraphBytecode& Bytecode,
        std::string_view EventName = "BeginPlay");
    const FScriptExecutionReport& GetLastExecutionReport() const;
    EScriptComponentExecutionState GetExecutionState() const;
    EScriptLatentAction GetActiveLatentAction() const;
    float GetLatentRemainingSeconds() const;
    const std::vector<FScriptScreenMessage>& GetScreenMessages() const;
    bool CancelExecution();
    void TickComponent(float DeltaSeconds) override;

protected:
    explicit PScriptComponent(const FObjectConstructionParams& Params);
    void OnRegister() override;
    void OnUnregister() override;

private:
    FAssetPath GraphAsset;
    bool bExecuteOnBeginPlay = true;
    int32 MaxInstructions = 1024;
    int32 MaxLoopIterations = 64;
    int32 MaxCallDepth = 16;
    int32 ExecutionStateValue = static_cast<int32>(EScriptComponentExecutionState::Idle);
    int32 ActiveLatentActionValue = static_cast<int32>(EScriptLatentAction::None);
    int32 LastInstructionsExecuted = 0;
    FScriptExecutionReport LastExecutionReport;
    FPicoGraphBytecode ActiveBytecode;
    std::uint32_t PendingContinuation = UINT32_MAX;
    std::uint32_t PendingAlternateContinuation = UINT32_MAX;
    float LatentRemainingSeconds = 0.0f;
    uint64 ExecutionGeneration = 0;
    std::function<void()> CancelPendingAction;
    std::vector<FScriptScreenMessage> ScreenMessages;

    FScriptExecutionReport ExecuteContinuation(std::uint32_t Instruction);
    void ApplyExecutionReport(FScriptExecutionReport Report);
    void AddScreenMessage(std::string Message, float DurationSeconds);
    void CompleteLatentAction(uint64 Generation, bool bSucceeded, std::string Message);
};
}
