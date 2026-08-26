#include "Pico/Engine/ScriptComponent.h"

#include "Pico/Core/Paths.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Graph/GraphAsset.h"
#include "Pico/Graph/GraphCompiler.h"
#include "Pico/Object/Class.h"

#include <algorithm>
#include <filesystem>
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

    std::vector<PFunction> Functions;
    PICO_ADD_FUNCTION(Functions, ExecuteBeginPlayGraph, EFunctionFlags::Callable);
    return Class.AddProperties(std::move(Properties))
        && Class.AddFunctions(std::move(Functions));
}

PScriptComponent::PScriptComponent(const FObjectConstructionParams& Params)
    : PActorComponent(Params)
{
    PrimaryComponentTick.SetCanEverTick(false);
}

const FAssetPath& PScriptComponent::GetGraphAsset() const { return GraphAsset; }
void PScriptComponent::SetGraphAsset(const FAssetPath& AssetPath) { GraphAsset = AssetPath; }
bool PScriptComponent::GetExecuteOnBeginPlay() const { return bExecuteOnBeginPlay; }
void PScriptComponent::SetExecuteOnBeginPlay(bool bValue) { bExecuteOnBeginPlay = bValue; }
const FScriptExecutionReport& PScriptComponent::GetLastExecutionReport() const
{
    return LastExecutionReport;
}

void PScriptComponent::OnRegister()
{
    PActorComponent::OnRegister();
    if (bExecuteOnBeginPlay && GetOwner() != nullptr && GetOwner()->HasBegunPlay())
        ExecuteBeginPlayGraph();
}

bool PScriptComponent::ExecuteBeginPlayGraph()
{
    return ExecuteEvent("BeginPlay").Succeeded();
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
    FScriptExecutionContext Context;
    Context.Self = GetOwner();
    Context.EntryEvent = EventName;
    Context.Limits.MaxInstructions = static_cast<std::size_t>(std::max(1, MaxInstructions));
    Context.Limits.MaxLoopIterations = static_cast<std::size_t>(std::max(1, MaxLoopIterations));
    Context.Limits.MaxCallDepth = static_cast<std::size_t>(std::max(1, MaxCallDepth));
    LastExecutionReport = FPicoScriptVM().Execute(Bytecode, Context);
    return LastExecutionReport;
}
}
