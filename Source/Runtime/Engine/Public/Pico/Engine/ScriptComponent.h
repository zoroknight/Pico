#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Graph/ScriptVM.h"

namespace Pico
{
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

protected:
    explicit PScriptComponent(const FObjectConstructionParams& Params);
    void OnRegister() override;

private:
    FAssetPath GraphAsset;
    bool bExecuteOnBeginPlay = true;
    int32 MaxInstructions = 1024;
    int32 MaxLoopIterations = 64;
    int32 MaxCallDepth = 16;
    FScriptExecutionReport LastExecutionReport;
};
}
