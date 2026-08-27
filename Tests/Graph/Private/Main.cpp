#include "TestRunner.h"

#include "Pico/Graph/GraphAsset.h"
#include "Pico/Graph/GraphCompiler.h"
#include "Pico/Graph/ScriptVM.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Object/ReflectionMacros.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
class PGraphVMFixture final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PGraphVMFixture, Pico::PObject)

public:
    Pico::int32 Value = 0;
    bool bDelayAction = true;
    float DelaySeconds = 2.5f;
    Pico::int32 FunctionCalls = 0;
    Pico::int32 DelegateCalls = 0;
    Pico::TDynamicMulticastDelegate<void()> OnExecuted;

    void IncrementFunctionCalls() { ++FunctionCalls; }
    void RecordDelegateCall() { ++DelegateCalls; }

protected:
    explicit PGraphVMFixture(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }
};

PICO_DEFINE_CLASS(PGraphVMFixture)

bool PGraphVMFixture::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Value);
    PICO_ADD_PROPERTY(Properties, bDelayAction);
    PICO_ADD_PROPERTY(Properties, DelaySeconds);
    PICO_ADD_PROPERTY(Properties, OnExecuted);
    std::vector<Pico::PFunction> Functions;
    PICO_ADD_FUNCTION(Functions, IncrementFunctionCalls, Pico::EFunctionFlags::Callable);
    PICO_ADD_FUNCTION(Functions, RecordDelegateCall, Pico::EFunctionFlags::Callable);
    return Class.AddProperties(std::move(Properties))
        && Class.AddFunctions(std::move(Functions));
}

Pico::FGraphPin* Pin(Pico::FGraphNode& Node, std::string_view Name)
{
    const auto Found = std::find_if(Node.Pins.begin(), Node.Pins.end(),
        [Name](const Pico::FGraphPin& Value) { return Value.Name == Name; });
    return Found == Node.Pins.end() ? nullptr : &*Found;
}

std::string ReadAll(const std::filesystem::path& FilePath)
{
    std::ifstream File(FilePath, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(File),
        std::istreambuf_iterator<char>());
}

Pico::FPicoGraphAsset MakeGraph()
{
    Pico::FPicoGraphAsset Graph;
    Graph.GraphId = Pico::CreateGraphStableId();
    Graph.Variables.push_back(Pico::FGraphVariable {
        Pico::CreateGraphStableId(), "DoorSpeed",
        Pico::EGraphValueType::Float, "120.0"});
    Graph.Nodes.push_back(Pico::MakeEntryEventNode("BeginPlay", 40.0f, 80.0f));
    Graph.Nodes.push_back(Pico::MakeGraphNode("Sequence", "Sequence", 300.0f, 80.0f));
    Pico::EGraphAssetError Error = Pico::EGraphAssetError::None;
    const bool bLinked = Pico::AddGraphLink(
        Graph,
        Graph.Nodes[0].Pins[0].Id,
        Graph.Nodes[1].Pins[0].Id,
        &Error);
    return bLinked ? Graph : Pico::FPicoGraphAsset {};
}

bool HasDiagnostic(
    const Pico::FGraphCompileResult& Result,
    Pico::EGraphDiagnosticCode Code)
{
    return std::any_of(Result.Diagnostics.begin(), Result.Diagnostics.end(),
        [Code](const Pico::FGraphDiagnostic& Diagnostic)
        {
            return Diagnostic.Code == Code;
        });
}
}

int main()
{
    FTestRunner Runner;
    Pico::PObjectSystem::Init();
    PGraphVMFixture::RegisterClass();
    Pico::FPicoGraphAsset Graph = MakeGraph();
    Pico::EGraphAssetError Error = Pico::EGraphAssetError::None;
    Runner.Expect(
        Pico::ValidateGraphAsset(Graph, &Error)
            && Graph.Nodes.size() == 2 && Graph.Links.size() == 1,
        "PicoGraph accepts stable node, pin, variable, and link identities");

    const std::filesystem::path First = std::filesystem::temp_directory_path()
        / "PicoGraphFirst.pgraph";
    const std::filesystem::path Second = std::filesystem::temp_directory_path()
        / "PicoGraphSecond.pgraph";
    Pico::FPicoGraphAsset Loaded;
    Runner.Expect(
        Pico::SaveGraphAssetToFile(First, Graph, &Error)
            && Pico::LoadGraphAssetFromFile(First, Loaded, &Error)
            && Pico::SaveGraphAssetToFile(Second, Loaded, &Error)
            && ReadAll(First) == ReadAll(Second)
            && Loaded.GraphId == Graph.GraphId
            && Loaded.Nodes[0].Id == Graph.Nodes[0].Id
            && Loaded.Nodes[0].Pins[0].Id == Graph.Nodes[0].Pins[0].Id,
        ".pgraph serialization is deterministic and preserves stable identities");

    Pico::FPicoGraphAsset Invalid = Graph;
    Invalid.Nodes[1].Id = Invalid.Nodes[0].Id;
    Runner.Expect(
        !Pico::ValidateGraphAsset(Invalid, &Error)
            && Error == Pico::EGraphAssetError::DuplicateId,
        "PicoGraph rejects duplicate stable identities");

    Invalid = Graph;
    Invalid.Links[0].InputPinId = "missing";
    Runner.Expect(
        !Pico::ValidateGraphAsset(Invalid, &Error)
            && Error == Pico::EGraphAssetError::MissingPin,
        "PicoGraph rejects links to missing pins");

    const std::string RemovedNodeId = Graph.Nodes[1].Id;
    Runner.Expect(
        Pico::RemoveGraphNode(Graph, RemovedNodeId)
            && Graph.Nodes.size() == 1 && Graph.Links.empty()
            && Pico::ValidateGraphAsset(Graph, &Error),
        "Removing a node also removes its links without invalidating the graph");

    Pico::FGraphTransactionHistory History(4);
    const Pico::FPicoGraphAsset Before = Graph;
    Graph.Nodes.push_back(Pico::MakeGraphNode("Branch", "Branch", 500.0f, 100.0f));
    History.Record(Before);
    const std::string AddedNodeId = Graph.Nodes.back().Id;
    Runner.Expect(
        History.CanUndo() && History.Undo(Graph)
            && Graph.Nodes.size() == 1 && History.CanRedo()
            && History.Redo(Graph) && Graph.Nodes.back().Id == AddedNodeId,
        "Graph transactions restore stable graph snapshots for Undo and Redo");

    const Pico::FPicoGraphAsset Compilable = MakeGraph();
    const Pico::FGraphCompileResult FirstCompile = Pico::CompileGraph(Compilable);
    const Pico::FGraphCompileResult SecondCompile = Pico::CompileGraph(Compilable);
    Runner.Expect(
        FirstCompile.bSucceeded
            && FirstCompile.Diagnostics.empty()
            && FirstCompile.IR.Variables.size() == 1
            && FirstCompile.IR.Variables[0].Name == "DoorSpeed"
            && FirstCompile.IR.EntryInstructions.size() == 1
            && FirstCompile.IR.Instructions.size() == 2
            && FirstCompile.Bytecode.Bytes == SecondCompile.Bytecode.Bytes
            && FirstCompile.Bytecode.Bytes.size() > 8
            && FirstCompile.Bytecode.Bytes[0] == 'P'
            && FirstCompile.Bytecode.Bytes[1] == 'G'
            && FirstCompile.Bytecode.Bytes[2] == 'R'
            && FirstCompile.Bytecode.Bytes[3] == 'B',
        "A valid graph produces deterministic typed IR and versioned PGRB bytecode");

    Pico::FPicoGraphAsset ChangedVariable = Compilable;
    ChangedVariable.Variables[0].DefaultValue = "240.0";
    Runner.Expect(
        Pico::CompileGraph(ChangedVariable).Bytecode.Bytes != FirstCompile.Bytecode.Bytes,
        "A typed variable default participates in deterministic bytecode output");

    Pico::FPicoGraphAsset Reordered = Compilable;
    std::reverse(Reordered.Nodes.begin(), Reordered.Nodes.end());
    Reordered.Nodes[0].PositionX += 900.0f;
    Reordered.Nodes[1].PositionY -= 400.0f;
    Runner.Expect(
        Pico::CompileGraph(Reordered).Bytecode.Bytes == FirstCompile.Bytecode.Bytes,
        "Bytecode ordering is stable and ignores editor-only node layout");

    Pico::FPicoGraphAsset TypedGraph;
    TypedGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode Entry;
    Pico::FGraphNode Branch;
    Pico::FGraphNode BoolValue;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, Entry);
    Entry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("Branch", 200.0f, 0.0f, Branch);
    Pico::MakeSchemaGraphNode("BoolLiteral", 0.0f, 180.0f, BoolValue);
    BoolValue.Pins[0].DefaultValue = "true";
    TypedGraph.Nodes = {Entry, Branch, BoolValue};
    Pico::AddGraphLink(TypedGraph, Entry.Pins[0].Id, Branch.Pins[0].Id, &Error);
    Pico::AddGraphLink(TypedGraph, BoolValue.Pins[0].Id, Branch.Pins[1].Id, &Error);
    const Pico::FGraphCompileResult TypedCompile = Pico::CompileGraph(TypedGraph);
    const auto BranchIR = std::find_if(
        TypedCompile.IR.Instructions.begin(), TypedCompile.IR.Instructions.end(),
        [](const Pico::FGraphIRInstruction& Instruction)
        {
            return Instruction.Opcode == Pico::EGraphIROpcode::Branch;
        });
    Runner.Expect(
        TypedCompile.bSucceeded && BranchIR != TypedCompile.IR.Instructions.end()
            && BranchIR->Operands.size() == 1
            && BranchIR->Operands[0].Type == Pico::EGraphValueType::Bool
            && BranchIR->Operands[0].Source == Pico::EGraphIRValueSource::LinkedPin
            && BranchIR->Operands[0].SourceNodeId == BoolValue.Id,
        "Typed IR preserves a validated Bool data dependency into Branch");

    Pico::FPicoGraphAsset UnknownNode = Compilable;
    UnknownNode.Nodes[1].TypeName = "DoesNotExist";
    const Pico::FGraphCompileResult UnknownCompile = Pico::CompileGraph(UnknownNode);
    Runner.Expect(
        !UnknownCompile.bSucceeded && UnknownCompile.Bytecode.Bytes.empty()
            && HasDiagnostic(UnknownCompile, Pico::EGraphDiagnosticCode::UnknownNodeType),
        "An unknown node type is rejected before bytecode generation");

    Pico::FPicoGraphAsset InvalidPins = Compilable;
    InvalidPins.Nodes[1].Pins[0].Name = "WrongPin";
    const Pico::FGraphCompileResult InvalidPinCompile = Pico::CompileGraph(InvalidPins);
    Runner.Expect(
        !InvalidPinCompile.bSucceeded
            && HasDiagnostic(InvalidPinCompile, Pico::EGraphDiagnosticCode::InvalidNodePins),
        "A node whose pins drift from its Schema cannot compile");

    Pico::FPicoGraphAsset InvalidDefault = TypedGraph;
    InvalidDefault.Links.erase(InvalidDefault.Links.begin() + 1);
    InvalidDefault.Nodes[1].Pins[1].DefaultValue = "not-a-bool";
    const Pico::FGraphCompileResult InvalidDefaultCompile = Pico::CompileGraph(InvalidDefault);
    Runner.Expect(
        !InvalidDefaultCompile.bSucceeded
            && HasDiagnostic(InvalidDefaultCompile,
                Pico::EGraphDiagnosticCode::InvalidDefaultValue),
        "An unlinked data pin must provide a value matching its declared type");

    Pico::FPicoGraphAsset DuplicateVariable = Compilable;
    DuplicateVariable.Variables.push_back(Pico::FGraphVariable {
        Pico::CreateGraphStableId(), "DoorSpeed", Pico::EGraphValueType::Float, "1.0"});
    const Pico::FGraphCompileResult DuplicateVariableCompile = Pico::CompileGraph(DuplicateVariable);
    Runner.Expect(
        !DuplicateVariableCompile.bSucceeded
            && HasDiagnostic(DuplicateVariableCompile,
                Pico::EGraphDiagnosticCode::DuplicateVariableName),
        "Graph variables require unique names before compilation");

    Pico::FPicoGraphAsset CycleGraph;
    CycleGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode CycleEntry;
    Pico::FGraphNode FirstSequence;
    Pico::FGraphNode SecondSequence;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, CycleEntry);
    Pico::MakeSchemaGraphNode("Sequence", 200.0f, 0.0f, FirstSequence);
    Pico::MakeSchemaGraphNode("Sequence", 400.0f, 0.0f, SecondSequence);
    CycleGraph.Nodes = {CycleEntry, FirstSequence, SecondSequence};
    Pico::AddGraphLink(CycleGraph, FirstSequence.Pins[1].Id, SecondSequence.Pins[0].Id, &Error);
    Pico::AddGraphLink(CycleGraph, SecondSequence.Pins[1].Id, FirstSequence.Pins[0].Id, &Error);
    const Pico::FGraphCompileResult CycleCompile = Pico::CompileGraph(CycleGraph);
    Runner.Expect(
        !CycleCompile.bSucceeded
            && HasDiagnostic(CycleCompile, Pico::EGraphDiagnosticCode::ControlFlowCycle),
        "PicoGraph Lite rejects control-flow cycles before the VM exists");

    Pico::FPicoGraphAsset MissingEntry = Compilable;
    const auto EntryNode = std::find_if(
        MissingEntry.Nodes.begin(), MissingEntry.Nodes.end(),
        [](const Pico::FGraphNode& Node) { return Node.TypeName == "EntryEvent"; });
    const std::string EntryNodeId = EntryNode != MissingEntry.Nodes.end()
        ? EntryNode->Id : std::string {};
    const bool bRemovedEntry = Pico::RemoveGraphNode(MissingEntry, EntryNodeId);
    const Pico::FGraphCompileResult MissingEntryCompile = Pico::CompileGraph(MissingEntry);
    Runner.Expect(
        bRemovedEntry && !MissingEntryCompile.bSucceeded
            && HasDiagnostic(MissingEntryCompile,
                Pico::EGraphDiagnosticCode::MissingEntryEvent),
        "Deleting the final Entry node remains editable but cannot compile");

    Pico::FPicoGraphAsset RuntimeGraph;
    RuntimeGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode RuntimeEntry;
    Pico::FGraphNode SetProperty;
    Pico::FGraphNode CallFunction;
    Pico::FGraphNode Broadcast;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, RuntimeEntry);
    RuntimeEntry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("SetProperty", 200.0f, 0.0f, SetProperty);
    Pico::MakeSchemaGraphNode("CallFunction", 400.0f, 0.0f, CallFunction);
    Pico::MakeSchemaGraphNode("BroadcastDelegate", 600.0f, 0.0f, Broadcast);
    Pin(SetProperty, "PropertyName")->DefaultValue = "Value";
    Pin(SetProperty, "Value")->DefaultValue = "42";
    Pin(CallFunction, "FunctionName")->DefaultValue = "IncrementFunctionCalls";
    Pin(Broadcast, "DelegateName")->DefaultValue = "OnExecuted";
    RuntimeGraph.Nodes = {RuntimeEntry, SetProperty, CallFunction, Broadcast};
    Pico::AddGraphLink(RuntimeGraph, Pin(RuntimeGraph.Nodes[0], "Then")->Id,
        Pin(RuntimeGraph.Nodes[1], "In")->Id, &Error);
    Pico::AddGraphLink(RuntimeGraph, Pin(RuntimeGraph.Nodes[1], "Then")->Id,
        Pin(RuntimeGraph.Nodes[2], "In")->Id, &Error);
    Pico::AddGraphLink(RuntimeGraph, Pin(RuntimeGraph.Nodes[2], "Then")->Id,
        Pin(RuntimeGraph.Nodes[3], "In")->Id, &Error);
    const Pico::FGraphCompileResult RuntimeCompile = Pico::CompileGraph(RuntimeGraph);
    PGraphVMFixture* Fixture = Pico::NewObject<PGraphVMFixture>(nullptr, "GraphVMFixture");
    const Pico::PProperty* DelegateProperty = Fixture != nullptr
        ? Fixture->GetClass()->FindProperty(Pico::FName("OnExecuted")) : nullptr;
    Pico::FDynamicMulticastDelegate* Delegate = DelegateProperty != nullptr
        ? DelegateProperty->GetDynamicMulticastDelegate(Fixture) : nullptr;
    const bool bBound = Delegate != nullptr
        && Delegate->AddDynamic(Fixture, Pico::FName("RecordDelegateCall")).IsSuccess();
    Pico::FScriptExecutionContext RuntimeContext;
    RuntimeContext.Self = Fixture;
    const Pico::FScriptExecutionReport RuntimeReport =
        Pico::FPicoScriptVM().Execute(RuntimeCompile.Bytecode, RuntimeContext);
    Runner.Expect(
        RuntimeCompile.bSucceeded && bBound && RuntimeReport.Succeeded()
            && Fixture->Value == 42 && Fixture->FunctionCalls == 1
            && Fixture->DelegateCalls == 1,
        "FPicoScriptVM executes reflected Property, PFunction, and dynamic Delegate nodes");

    Pico::FPicoGraphIR LoopProgram;
    LoopProgram.GraphId = "LoopBudget";
    LoopProgram.EntryInstructions = {0};
    LoopProgram.Instructions.push_back(Pico::FGraphIRInstruction {
        "Loop", "BeginPlay", Pico::EGraphIROpcode::EntryEvent,
        {Pico::FGraphIRExecTarget {"Then", 0}}, {}});
    Pico::FScriptExecutionContext LoopContext;
    LoopContext.Limits.MaxInstructions = 100;
    LoopContext.Limits.MaxLoopIterations = 3;
    const Pico::FScriptExecutionReport LoopReport =
        Pico::FPicoScriptVM().Execute(LoopProgram, LoopContext);
    Runner.Expect(
        LoopReport.Result == Pico::EScriptExecutionResult::LoopBudgetExceeded
            && LoopReport.InstructionsExecuted == 4,
        "A cyclic or hostile graph is stopped by the per-instruction loop budget");
    LoopContext.Limits.MaxInstructions = 2;
    LoopContext.Limits.MaxLoopIterations = 100;
    Runner.Expect(
        Pico::FPicoScriptVM().Execute(LoopProgram, LoopContext).Result
            == Pico::EScriptExecutionResult::InstructionBudgetExceeded,
        "A graph is stopped when its total instruction budget is exhausted");

    Pico::FPicoGraphIR DepthProgram;
    DepthProgram.GraphId = "DepthBudget";
    DepthProgram.EntryInstructions = {0};
    DepthProgram.Instructions = {
        {"Entry", "BeginPlay", Pico::EGraphIROpcode::EntryEvent,
            {Pico::FGraphIRExecTarget {"Then", 1}}, {}},
        {"Set", "Set", Pico::EGraphIROpcode::SetProperty, {}, {
            {"name", "PropertyName", Pico::EGraphPinDirection::Input,
                Pico::EGraphValueType::String, Pico::EGraphIRValueSource::DefaultValue, "Value", {}, {}},
            {"input", "Value", Pico::EGraphPinDirection::Input,
                Pico::EGraphValueType::String, Pico::EGraphIRValueSource::LinkedPin, {}, "Get", "out"}}},
        {"Get", "Get", Pico::EGraphIROpcode::GetProperty, {}, {
            {"property", "PropertyName", Pico::EGraphPinDirection::Input,
                Pico::EGraphValueType::String, Pico::EGraphIRValueSource::LinkedPin, {}, "Get", "out"},
            {"out", "Value", Pico::EGraphPinDirection::Output,
                Pico::EGraphValueType::String, Pico::EGraphIRValueSource::DefaultValue, {}, {}, {}}}}
    };
    Pico::FScriptExecutionContext DepthContext;
    DepthContext.Self = Fixture;
    DepthContext.Limits.MaxCallDepth = 4;
    const Pico::FScriptExecutionReport DepthReport =
        Pico::FPicoScriptVM().Execute(DepthProgram, DepthContext);
    Runner.Expect(
        DepthReport.Result == Pico::EScriptExecutionResult::CallDepthExceeded,
        "A recursive data dependency is stopped by the call-depth budget");

    Pico::FPicoGraphBytecode Corrupt = RuntimeCompile.Bytecode;
    Corrupt.Bytes[0] = 'X';
    Runner.Expect(
        Pico::FPicoScriptVM().Execute(Corrupt, RuntimeContext).Result
            == Pico::EScriptExecutionResult::InvalidProgram,
        "Malformed PGRB bytecode is rejected before execution");

    Pico::FPicoGraphAsset LatentGraph;
    LatentGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode LatentEntry;
    Pico::FGraphNode Delay;
    Pico::FGraphNode LatentSet;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, LatentEntry);
    LatentEntry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("Delay", 200.0f, 0.0f, Delay);
    Pico::MakeSchemaGraphNode("SetProperty", 400.0f, 0.0f, LatentSet);
    Pin(Delay, "Seconds")->DefaultValue = "0.25";
    Pin(LatentSet, "PropertyName")->DefaultValue = "Value";
    Pin(LatentSet, "Value")->DefaultValue = "88";
    LatentGraph.Nodes = {LatentEntry, Delay, LatentSet};
    Pico::AddGraphLink(LatentGraph, Pin(LatentGraph.Nodes[0], "Then")->Id,
        Pin(LatentGraph.Nodes[1], "In")->Id, &Error);
    Pico::AddGraphLink(LatentGraph, Pin(LatentGraph.Nodes[1], "Completed")->Id,
        Pin(LatentGraph.Nodes[2], "In")->Id, &Error);
    const Pico::FGraphCompileResult LatentCompile = Pico::CompileGraph(LatentGraph);
    Fixture->Value = 0;
    const Pico::FScriptExecutionReport Suspended =
        Pico::FPicoScriptVM().Execute(LatentCompile.Bytecode, RuntimeContext);
    Pico::FScriptExecutionContext ResumeContext = RuntimeContext;
    ResumeContext.StartInstruction = Suspended.ContinuationInstruction;
    const Pico::FScriptExecutionReport Resumed =
        Pico::FPicoScriptVM().Execute(LatentCompile.Bytecode, ResumeContext);
    Runner.Expect(
        LatentCompile.bSucceeded && Suspended.IsSuspended()
            && Suspended.LatentAction == Pico::EScriptLatentAction::Delay
            && Suspended.LatentSeconds == 0.25f && Fixture->Value == 88
            && Resumed.Succeeded(),
        "PGRB v3 suspends at Delay and resumes from an explicit continuation");

    Pico::FPicoGraphAsset ParameterGraph;
    ParameterGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode ParameterEntry;
    Pico::FGraphNode ParameterBranch;
    Pico::FGraphNode GetDelayEnabled;
    Pico::FGraphNode ParameterDelay;
    Pico::FGraphNode GetDelaySeconds;
    Pico::FGraphNode ImmediatePrint;
    Pico::FGraphNode DelayedPrint;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, ParameterEntry);
    ParameterEntry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("Branch", 200.0f, 0.0f, ParameterBranch);
    Pico::MakeSchemaGraphNode("GetBoolProperty", 0.0f, 180.0f, GetDelayEnabled);
    Pico::MakeSchemaGraphNode("Delay", 400.0f, 0.0f, ParameterDelay);
    Pico::MakeSchemaGraphNode("GetFloatProperty", 200.0f, 260.0f, GetDelaySeconds);
    Pico::MakeSchemaGraphNode("PrintString", 400.0f, 220.0f, ImmediatePrint);
    Pico::MakeSchemaGraphNode("PrintString", 620.0f, 0.0f, DelayedPrint);
    Pin(GetDelayEnabled, "PropertyName")->DefaultValue = "bDelayAction";
    Pin(GetDelaySeconds, "PropertyName")->DefaultValue = "DelaySeconds";
    Pin(ImmediatePrint, "Message")->DefaultValue = "immediate";
    Pin(DelayedPrint, "Message")->DefaultValue = "delayed";
    ParameterGraph.Nodes = {ParameterEntry, ParameterBranch, GetDelayEnabled,
        ParameterDelay, GetDelaySeconds, ImmediatePrint, DelayedPrint};
    Pico::AddGraphLink(ParameterGraph, Pin(ParameterGraph.Nodes[0], "Then")->Id,
        Pin(ParameterGraph.Nodes[1], "In")->Id, &Error);
    Pico::AddGraphLink(ParameterGraph, Pin(ParameterGraph.Nodes[2], "Value")->Id,
        Pin(ParameterGraph.Nodes[1], "Condition")->Id, &Error);
    Pico::AddGraphLink(ParameterGraph, Pin(ParameterGraph.Nodes[1], "True")->Id,
        Pin(ParameterGraph.Nodes[3], "In")->Id, &Error);
    Pico::AddGraphLink(ParameterGraph, Pin(ParameterGraph.Nodes[4], "Value")->Id,
        Pin(ParameterGraph.Nodes[3], "Seconds")->Id, &Error);
    Pico::AddGraphLink(ParameterGraph, Pin(ParameterGraph.Nodes[3], "Completed")->Id,
        Pin(ParameterGraph.Nodes[6], "In")->Id, &Error);
    Pico::AddGraphLink(ParameterGraph, Pin(ParameterGraph.Nodes[1], "False")->Id,
        Pin(ParameterGraph.Nodes[5], "In")->Id, &Error);
    const Pico::FGraphCompileResult ParameterCompile = Pico::CompileGraph(ParameterGraph);
    std::string PrintedMessage;
    Pico::FScriptExecutionContext ParameterContext = RuntimeContext;
    ParameterContext.PrintString = [&PrintedMessage](std::string Message, float)
    {
        PrintedMessage = std::move(Message);
    };
    Fixture->bDelayAction = true;
    Fixture->DelaySeconds = 2.5f;
    const Pico::FScriptExecutionReport ParameterSuspended =
        Pico::FPicoScriptVM().Execute(ParameterCompile.Bytecode, ParameterContext);
    ParameterContext.StartInstruction = ParameterSuspended.ContinuationInstruction;
    const Pico::FScriptExecutionReport ParameterResumed =
        Pico::FPicoScriptVM().Execute(ParameterCompile.Bytecode, ParameterContext);
    const bool bDelayedPathWorked = ParameterSuspended.IsSuspended()
        && ParameterSuspended.LatentSeconds == 2.5f
        && ParameterResumed.Succeeded() && PrintedMessage == "delayed";
    PrintedMessage.clear();
    Fixture->bDelayAction = false;
    ParameterContext.StartInstruction.reset();
    const Pico::FScriptExecutionReport ImmediateReport =
        Pico::FPicoScriptVM().Execute(ParameterCompile.Bytecode, ParameterContext);
    Runner.Expect(
        ParameterCompile.bSucceeded && bDelayedPathWorked
            && ImmediateReport.Succeeded() && PrintedMessage == "immediate",
        "Typed reflected Bool and Float inputs drive immediate and delayed PrintString paths");

    Pico::FPicoGraphAsset EventGraph;
    EventGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode EventEntry;
    Pico::FGraphNode WaitEvent;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, EventEntry);
    EventEntry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("WaitGameplayEvent", 200.0f, 0.0f, WaitEvent);
    Pin(WaitEvent, "AbilityHandle")->DefaultValue = "7";
    Pin(WaitEvent, "EventTag")->DefaultValue = "Event.Graph.Test";
    Pin(WaitEvent, "ExactMatch")->DefaultValue = "true";
    EventGraph.Nodes = {EventEntry, WaitEvent};
    Pico::AddGraphLink(EventGraph, Pin(EventGraph.Nodes[0], "Then")->Id,
        Pin(EventGraph.Nodes[1], "In")->Id, &Error);
    const Pico::FScriptExecutionReport EventReport = Pico::FPicoScriptVM().Execute(
        Pico::CompileGraph(EventGraph).Bytecode, RuntimeContext);

    Pico::FPicoGraphAsset MontageGraph;
    MontageGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode MontageEntry;
    Pico::FGraphNode Montage;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, MontageEntry);
    MontageEntry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("PlayMontageAndWait", 200.0f, 0.0f, Montage);
    Pin(Montage, "AbilityHandle")->DefaultValue = "8";
    Pin(Montage, "MontageAsset")->DefaultValue = "/Game/Animations/Test.pmontage";
    Pin(Montage, "PlayRate")->DefaultValue = "1.5";
    MontageGraph.Nodes = {MontageEntry, Montage};
    Pico::AddGraphLink(MontageGraph, Pin(MontageGraph.Nodes[0], "Then")->Id,
        Pin(MontageGraph.Nodes[1], "In")->Id, &Error);
    const Pico::FScriptExecutionReport MontageReport = Pico::FPicoScriptVM().Execute(
        Pico::CompileGraph(MontageGraph).Bytecode, RuntimeContext);

    Pico::FPicoGraphAsset AbilityGraph;
    AbilityGraph.GraphId = Pico::CreateGraphStableId();
    Pico::FGraphNode AbilityEntry;
    Pico::FGraphNode Activate;
    Pico::MakeSchemaGraphNode("EntryEvent", 0.0f, 0.0f, AbilityEntry);
    AbilityEntry.DisplayName = "BeginPlay";
    Pico::MakeSchemaGraphNode("ActivateAbility", 200.0f, 0.0f, Activate);
    Pin(Activate, "AbilityHandle")->DefaultValue = "9";
    AbilityGraph.Nodes = {AbilityEntry, Activate};
    Pico::AddGraphLink(AbilityGraph, Pin(AbilityGraph.Nodes[0], "Then")->Id,
        Pin(AbilityGraph.Nodes[1], "In")->Id, &Error);
    Pico::int32 ActivatedHandle = 0;
    Pico::FScriptExecutionContext AbilityContext = RuntimeContext;
    AbilityContext.ActivateAbility = [&ActivatedHandle](Pico::int32 Handle)
    {
        ActivatedHandle = Handle;
        return true;
    };
    const Pico::FScriptExecutionReport AbilityReport = Pico::FPicoScriptVM().Execute(
        Pico::CompileGraph(AbilityGraph).Bytecode, AbilityContext);
    Runner.Expect(
        EventReport.IsSuspended()
            && EventReport.LatentAction == Pico::EScriptLatentAction::WaitGameplayEvent
            && EventReport.AbilityHandle == 7 && EventReport.bExactMatch
            && EventReport.LatentPayload == "Event.Graph.Test"
            && MontageReport.IsSuspended()
            && MontageReport.LatentAction == Pico::EScriptLatentAction::PlayMontageAndWait
            && MontageReport.AbilityHandle == 8
            && MontageReport.LatentPayload == "/Game/Animations/Test.pmontage"
            && MontageReport.LatentPlayRate == 1.5f
            && AbilityReport.Succeeded() && ActivatedHandle == 9,
        "Gameplay Graph nodes preserve event, Montage, and Ability activation contracts");

    const std::filesystem::path LatentSource =
        std::filesystem::temp_directory_path() / "PicoGraphCookSource.pgraph";
    const std::filesystem::path LatentCooked =
        std::filesystem::temp_directory_path() / "PicoGraphCookSource.pgraph.pgrb";
    std::string CookError;
    Pico::FPicoGraphBytecode LoadedCooked;
    Runner.Expect(
        Pico::SaveGraphAssetToFile(LatentSource, LatentGraph, &Error)
            && Pico::CookGraphAsset(LatentSource, LatentCooked, &CookError)
            && Pico::LoadGraphBytecodeFromFile(LatentCooked, LoadedCooked, &CookError)
            && LoadedCooked.Bytes == LatentCompile.Bytecode.Bytes,
        "Graph Cook produces validated deterministic PGRB v3 for Runtime loading");

    std::error_code FileError;
    std::filesystem::remove(First, FileError);
    std::filesystem::remove(Second, FileError);
    std::filesystem::remove(LatentSource, FileError);
    std::filesystem::remove(LatentCooked, FileError);
    const int Result = Runner.Finish();
    Pico::PObjectSystem::Shutdown();
    return Result;
}
