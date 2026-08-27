#include "Pico/Graph/GraphCompiler.h"
#include "Pico/Graph/ScriptVM.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Pico
{
namespace
{
struct FPinOwner
{
    const FGraphNode* Node = nullptr;
    const FGraphPin* Pin = nullptr;
};

void AddDiagnostic(
    std::vector<FGraphDiagnostic>& Diagnostics,
    EGraphDiagnosticSeverity Severity,
    EGraphDiagnosticCode Code,
    std::string Message,
    std::string NodeId = {},
    std::string PinId = {})
{
    Diagnostics.push_back(FGraphDiagnostic {
        Severity, Code, std::move(Message), std::move(NodeId), std::move(PinId)});
}

bool HasErrors(const std::vector<FGraphDiagnostic>& Diagnostics)
{
    return std::any_of(Diagnostics.begin(), Diagnostics.end(),
        [](const FGraphDiagnostic& Diagnostic)
        {
            return Diagnostic.Severity == EGraphDiagnosticSeverity::Error;
        });
}

bool IsValidValue(EGraphValueType Type, std::string_view Text)
{
    switch (Type)
    {
    case EGraphValueType::Exec:
        return Text.empty();
    case EGraphValueType::Bool:
        return Text == "true" || Text == "false";
    case EGraphValueType::Int:
    {
        if (Text.empty()) return false;
        std::int64_t Value = 0;
        const char* Begin = Text.data();
        const char* End = Begin + Text.size();
        const auto Result = std::from_chars(Begin, End, Value);
        return Result.ec == std::errc {} && Result.ptr == End;
    }
    case EGraphValueType::Float:
    {
        if (Text.empty()) return false;
        std::string Copy(Text);
        char* End = nullptr;
        const double Value = std::strtod(Copy.c_str(), &End);
        return End == Copy.c_str() + Copy.size() && std::isfinite(Value);
    }
    case EGraphValueType::Vector:
    {
        std::string Copy(Text);
        std::replace(Copy.begin(), Copy.end(), ',', ' ');
        std::istringstream Stream(Copy);
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        std::string Extra;
        return (Stream >> X >> Y >> Z) && !(Stream >> Extra)
            && std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z);
    }
    case EGraphValueType::String:
    case EGraphValueType::Object:
        return true;
    }
    return false;
}

std::unordered_map<std::string, FPinOwner> BuildPinOwners(const FPicoGraphAsset& Graph)
{
    std::unordered_map<std::string, FPinOwner> Owners;
    for (const FGraphNode& Node : Graph.Nodes)
        for (const FGraphPin& Pin : Node.Pins)
            Owners.emplace(Pin.Id, FPinOwner {&Node, &Pin});
    return Owners;
}

const FGraphPin* FindNodePin(const FGraphNode& Node, const FGraphPinSchema& Schema)
{
    const auto Found = std::find_if(Node.Pins.begin(), Node.Pins.end(),
        [&Schema](const FGraphPin& Pin)
        {
            return Pin.Name == Schema.Name
                && Pin.Direction == Schema.Direction;
        });
    return Found == Node.Pins.end() ? nullptr : &*Found;
}

std::vector<const FGraphNode*> SortedNodes(const FPicoGraphAsset& Graph)
{
    std::vector<const FGraphNode*> Nodes;
    Nodes.reserve(Graph.Nodes.size());
    for (const FGraphNode& Node : Graph.Nodes) Nodes.push_back(&Node);
    std::sort(Nodes.begin(), Nodes.end(),
        [](const FGraphNode* Left, const FGraphNode* Right)
        {
            return Left->Id < Right->Id;
        });
    return Nodes;
}

void AppendU32(std::vector<std::uint8_t>& Bytes, std::uint32_t Value)
{
    for (int Shift = 0; Shift < 32; Shift += 8)
        Bytes.push_back(static_cast<std::uint8_t>((Value >> Shift) & 0xffu));
}

void AppendString(std::vector<std::uint8_t>& Bytes, std::string_view Value)
{
    AppendU32(Bytes, static_cast<std::uint32_t>(Value.size()));
    Bytes.insert(Bytes.end(), Value.begin(), Value.end());
}

FPicoGraphBytecode EncodeBytecode(const FPicoGraphIR& IR)
{
    FPicoGraphBytecode Bytecode;
    Bytecode.SourceGraphId = IR.GraphId;
    std::vector<std::uint8_t>& Bytes = Bytecode.Bytes;
    Bytes.insert(Bytes.end(), {'P', 'G', 'R', 'B'});
    AppendU32(Bytes, Bytecode.Version);
    AppendString(Bytes, IR.GraphId);
    AppendU32(Bytes, static_cast<std::uint32_t>(IR.Variables.size()));
    for (const FGraphIRVariable& Variable : IR.Variables)
    {
        AppendString(Bytes, Variable.Id);
        AppendString(Bytes, Variable.Name);
        Bytes.push_back(static_cast<std::uint8_t>(Variable.Type));
        AppendString(Bytes, Variable.DefaultValue);
    }
    AppendU32(Bytes, static_cast<std::uint32_t>(IR.EntryInstructions.size()));
    for (const std::uint32_t Entry : IR.EntryInstructions) AppendU32(Bytes, Entry);
    AppendU32(Bytes, static_cast<std::uint32_t>(IR.Instructions.size()));
    for (const FGraphIRInstruction& Instruction : IR.Instructions)
    {
        Bytes.push_back(static_cast<std::uint8_t>(Instruction.Opcode));
        AppendString(Bytes, Instruction.NodeId);
        AppendString(Bytes, Instruction.DisplayName);
        AppendU32(Bytes, static_cast<std::uint32_t>(Instruction.ExecTargets.size()));
        for (const FGraphIRExecTarget& Target : Instruction.ExecTargets)
        {
            AppendString(Bytes, Target.PinName);
            AppendU32(Bytes, Target.InstructionIndex);
        }
        AppendU32(Bytes, static_cast<std::uint32_t>(Instruction.Operands.size()));
        for (const FGraphIROperand& Operand : Instruction.Operands)
        {
            AppendString(Bytes, Operand.PinId);
            AppendString(Bytes, Operand.PinName);
            Bytes.push_back(static_cast<std::uint8_t>(Operand.Direction));
            Bytes.push_back(static_cast<std::uint8_t>(Operand.Type));
            Bytes.push_back(static_cast<std::uint8_t>(Operand.Source));
            AppendString(Bytes, Operand.Value);
            AppendString(Bytes, Operand.SourceNodeId);
            AppendString(Bytes, Operand.SourcePinId);
        }
    }
    return Bytecode;
}
}

const FGraphNodeSchema* FGraphSchemaRegistry::Find(std::string_view TypeName) const
{
    const auto Found = std::find_if(Schemas.begin(), Schemas.end(),
        [TypeName](const FGraphNodeSchema& Schema)
        {
            return Schema.TypeName == TypeName;
        });
    return Found == Schemas.end() ? nullptr : &*Found;
}

const std::vector<FGraphNodeSchema>& FGraphSchemaRegistry::GetSchemas() const
{
    return Schemas;
}

const FGraphSchemaRegistry& GetDefaultGraphSchemaRegistry()
{
    static const FGraphSchemaRegistry Registry = []
    {
        FGraphSchemaRegistry Value;
        Value.Schemas = {
            {"EntryEvent", "Entry Event", EGraphIROpcode::EntryEvent,
                {{"Then", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"Sequence", "Sequence", EGraphIROpcode::Sequence,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"Then", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"Branch", "Branch", EGraphIROpcode::Branch,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"Condition", EGraphPinDirection::Input, EGraphValueType::Bool, "false"},
                 {"True", EGraphPinDirection::Output, EGraphValueType::Exec, {}},
                 {"False", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"BoolLiteral", "Bool Literal", EGraphIROpcode::BoolLiteral,
                {{"Value", EGraphPinDirection::Output, EGraphValueType::Bool, "false"}}},
            {"FloatLiteral", "Float Literal", EGraphIROpcode::FloatLiteral,
                {{"Value", EGraphPinDirection::Output, EGraphValueType::Float, "0.0"}}},
            {"AddFloat", "Add Float", EGraphIROpcode::AddFloat,
                {{"A", EGraphPinDirection::Input, EGraphValueType::Float, "0.0"},
                 {"B", EGraphPinDirection::Input, EGraphValueType::Float, "0.0"},
                 {"Result", EGraphPinDirection::Output, EGraphValueType::Float, {}}}},
            {"CallFunction", "Call Function", EGraphIROpcode::CallFunction,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"FunctionName", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Then", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"GetProperty", "Get Property", EGraphIROpcode::GetProperty,
                {{"PropertyName", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Value", EGraphPinDirection::Output, EGraphValueType::String, {}}}},
            {"GetBoolProperty", "Get Bool Property", EGraphIROpcode::GetBoolProperty,
                {{"PropertyName", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Value", EGraphPinDirection::Output, EGraphValueType::Bool, {}}}},
            {"GetFloatProperty", "Get Float Property", EGraphIROpcode::GetFloatProperty,
                {{"PropertyName", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Value", EGraphPinDirection::Output, EGraphValueType::Float, {}}}},
            {"SetProperty", "Set Property", EGraphIROpcode::SetProperty,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"PropertyName", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Value", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Then", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"BroadcastDelegate", "Broadcast Delegate", EGraphIROpcode::BroadcastDelegate,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"DelegateName", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"Then", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"Delay", "Delay", EGraphIROpcode::Delay,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"Seconds", EGraphPinDirection::Input, EGraphValueType::Float, "1.0"},
                 {"Completed", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"WaitGameplayEvent", "Wait Gameplay Event", EGraphIROpcode::WaitGameplayEvent,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"AbilityHandle", EGraphPinDirection::Input, EGraphValueType::Int, "0"},
                 {"EventTag", EGraphPinDirection::Input, EGraphValueType::String, "Event.Graph.Resume"},
                 {"ExactMatch", EGraphPinDirection::Input, EGraphValueType::Bool, "false"},
                 {"Received", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"PlayMontageAndWait", "Play Montage And Wait", EGraphIROpcode::PlayMontageAndWait,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"AbilityHandle", EGraphPinDirection::Input, EGraphValueType::Int, "0"},
                 {"MontageAsset", EGraphPinDirection::Input, EGraphValueType::String, {}},
                 {"PlayRate", EGraphPinDirection::Input, EGraphValueType::Float, "1.0"},
                 {"Completed", EGraphPinDirection::Output, EGraphValueType::Exec, {}},
                 {"Interrupted", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"ActivateAbility", "Activate Ability", EGraphIROpcode::ActivateAbility,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"AbilityHandle", EGraphPinDirection::Input, EGraphValueType::Int, "0"},
                 {"Succeeded", EGraphPinDirection::Output, EGraphValueType::Exec, {}},
                 {"Failed", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}},
            {"PrintString", "Print String", EGraphIROpcode::PrintString,
                {{"In", EGraphPinDirection::Input, EGraphValueType::Exec, {}},
                 {"Message", EGraphPinDirection::Input, EGraphValueType::String,
                    "Graph action executed"},
                 {"Duration", EGraphPinDirection::Input, EGraphValueType::Float, "5.0"},
                 {"Then", EGraphPinDirection::Output, EGraphValueType::Exec, {}}}}
        };
        return Value;
    }();
    return Registry;
}

bool MakeSchemaGraphNode(
    std::string_view TypeName,
    float X,
    float Y,
    FGraphNode& OutNode)
{
    const FGraphNodeSchema* Schema = GetDefaultGraphSchemaRegistry().Find(TypeName);
    if (Schema == nullptr) return false;
    FGraphNode Node;
    Node.Id = CreateGraphStableId();
    Node.TypeName = Schema->TypeName;
    Node.DisplayName = Schema->DisplayName;
    Node.PositionX = X;
    Node.PositionY = Y;
    for (const FGraphPinSchema& PinSchema : Schema->Pins)
    {
        Node.Pins.push_back(FGraphPin {
            CreateGraphStableId(), PinSchema.Name, PinSchema.Direction,
            PinSchema.Type, PinSchema.DefaultValue});
    }
    OutNode = std::move(Node);
    return true;
}

std::vector<FGraphDiagnostic> ValidateGraphSemantics(
    const FPicoGraphAsset& Graph,
    const FGraphSchemaRegistry& Registry)
{
    std::vector<FGraphDiagnostic> Diagnostics;
    EGraphAssetError AssetError = EGraphAssetError::None;
    if (!ValidateGraphAsset(Graph, &AssetError))
    {
        AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
            EGraphDiagnosticCode::InvalidAsset,
            "Graph asset structure is invalid: " + std::string(ToString(AssetError)));
        return Diagnostics;
    }

    std::unordered_set<std::string> VariableNames;
    for (const FGraphVariable& Variable : Graph.Variables)
    {
        if (!VariableNames.insert(Variable.Name).second)
            AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                EGraphDiagnosticCode::DuplicateVariableName,
                "Variable name is duplicated: " + Variable.Name);
        if (!IsValidValue(Variable.Type, Variable.DefaultValue))
            AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                EGraphDiagnosticCode::InvalidDefaultValue,
                "Variable default does not match " + std::string(ToString(Variable.Type))
                    + ": " + Variable.Name);
    }

    const auto Owners = BuildPinOwners(Graph);
    std::unordered_map<std::string, const FGraphLink*> Incoming;
    std::unordered_map<std::string, std::size_t> OutgoingCounts;
    for (const FGraphLink& Link : Graph.Links)
    {
        Incoming.emplace(Link.InputPinId, &Link);
        ++OutgoingCounts[Link.OutputPinId];
    }

    const std::vector<const FGraphNode*> Nodes = SortedNodes(Graph);
    std::vector<const FGraphNode*> Entries;
    for (const FGraphNode* Node : Nodes)
    {
        const FGraphNodeSchema* Schema = Registry.Find(Node->TypeName);
        if (Schema == nullptr)
        {
            AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                EGraphDiagnosticCode::UnknownNodeType,
                "No Graph Schema is registered for node type " + Node->TypeName,
                Node->Id);
            continue;
        }
        if (Schema->Opcode == EGraphIROpcode::EntryEvent) Entries.push_back(Node);
        bool bPinsValid = Node->Pins.size() == Schema->Pins.size();
        for (const FGraphPinSchema& PinSchema : Schema->Pins)
        {
            const FGraphPin* Pin = FindNodePin(*Node, PinSchema);
            if (Pin == nullptr || Pin->Type != PinSchema.Type)
            {
                bPinsValid = false;
                continue;
            }
            if (Pin->Type == EGraphValueType::Exec
                && Pin->Direction == EGraphPinDirection::Output
                && OutgoingCounts[Pin->Id] > 1)
            {
                AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                    EGraphDiagnosticCode::InvalidNodePins,
                    "Exec output may connect to only one input; use Sequence for fan-out",
                    Node->Id, Pin->Id);
            }
            const bool bUsesDefault = Pin->Type != EGraphValueType::Exec
                && (Pin->Direction == EGraphPinDirection::Output
                    || !Incoming.contains(Pin->Id));
            const bool bComputedOutput = Pin->Direction == EGraphPinDirection::Output
                && PinSchema.DefaultValue.empty();
            if (bUsesDefault && !bComputedOutput
                && !IsValidValue(Pin->Type, Pin->DefaultValue))
            {
                AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                    EGraphDiagnosticCode::InvalidDefaultValue,
                    "Pin default does not match " + std::string(ToString(Pin->Type))
                        + ": " + Pin->Name,
                    Node->Id, Pin->Id);
            }
        }
        if (!bPinsValid)
            AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                EGraphDiagnosticCode::InvalidNodePins,
                "Node pins do not match the registered Schema for " + Node->TypeName,
                Node->Id);
    }

    if (Entries.empty())
        AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
            EGraphDiagnosticCode::MissingEntryEvent,
            "Graph must contain at least one Entry Event");

    std::unordered_map<std::string, std::vector<std::string>> ExecEdges;
    for (const FGraphLink& Link : Graph.Links)
    {
        const auto Output = Owners.find(Link.OutputPinId);
        const auto Input = Owners.find(Link.InputPinId);
        if (Output != Owners.end() && Input != Owners.end()
            && Output->second.Pin->Type == EGraphValueType::Exec)
            ExecEdges[Output->second.Node->Id].push_back(Input->second.Node->Id);
    }
    for (auto& [NodeId, Targets] : ExecEdges)
    {
        static_cast<void>(NodeId);
        std::sort(Targets.begin(), Targets.end());
    }

    std::unordered_map<std::string, std::uint8_t> VisitState;
    bool bCycleReported = false;
    std::function<void(const std::string&)> DetectCycle = [&](const std::string& NodeId)
    {
        VisitState[NodeId] = 1;
        for (const std::string& Target : ExecEdges[NodeId])
        {
            if (VisitState[Target] == 1 && !bCycleReported)
            {
                AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Error,
                    EGraphDiagnosticCode::ControlFlowCycle,
                    "Control-flow cycle is not supported by PicoGraph Lite", Target);
                bCycleReported = true;
            }
            else if (VisitState[Target] == 0) DetectCycle(Target);
        }
        VisitState[NodeId] = 2;
    };
    for (const FGraphNode* Node : Nodes)
        if (VisitState[Node->Id] == 0) DetectCycle(Node->Id);

    std::unordered_set<std::string> Reachable;
    std::function<void(const std::string&)> MarkReachable = [&](const std::string& NodeId)
    {
        if (!Reachable.insert(NodeId).second) return;
        for (const std::string& Target : ExecEdges[NodeId]) MarkReachable(Target);
    };
    for (const FGraphNode* Entry : Entries) MarkReachable(Entry->Id);
    for (const FGraphNode* Node : Nodes)
    {
        const FGraphNodeSchema* Schema = Registry.Find(Node->TypeName);
        const bool bPureData = Schema != nullptr
            && std::none_of(Schema->Pins.begin(), Schema->Pins.end(),
                [](const FGraphPinSchema& Pin) { return Pin.Type == EGraphValueType::Exec; });
        if (!bPureData && !Reachable.contains(Node->Id))
            AddDiagnostic(Diagnostics, EGraphDiagnosticSeverity::Warning,
                EGraphDiagnosticCode::UnreachableNode,
                "Node is not reachable from an Entry Event", Node->Id);
    }
    return Diagnostics;
}

FGraphCompileResult CompileGraph(
    const FPicoGraphAsset& Graph,
    const FGraphSchemaRegistry& Registry)
{
    FGraphCompileResult Result;
    Result.Diagnostics = ValidateGraphSemantics(Graph, Registry);
    if (HasErrors(Result.Diagnostics)) return Result;

    Result.IR.GraphId = Graph.GraphId;
    std::vector<const FGraphVariable*> Variables;
    Variables.reserve(Graph.Variables.size());
    for (const FGraphVariable& Variable : Graph.Variables) Variables.push_back(&Variable);
    std::sort(Variables.begin(), Variables.end(),
        [](const FGraphVariable* Left, const FGraphVariable* Right)
        {
            return Left->Id < Right->Id;
        });
    for (const FGraphVariable* Variable : Variables)
    {
        Result.IR.Variables.push_back(FGraphIRVariable {
            Variable->Id, Variable->Name, Variable->Type, Variable->DefaultValue});
    }
    const std::vector<const FGraphNode*> Nodes = SortedNodes(Graph);
    std::unordered_map<std::string, std::uint32_t> NodeIndices;
    const auto Owners = BuildPinOwners(Graph);
    for (std::uint32_t Index = 0; Index < Nodes.size(); ++Index)
        NodeIndices.emplace(Nodes[Index]->Id, Index);

    for (const FGraphNode* Node : Nodes)
    {
        const FGraphNodeSchema* Schema = Registry.Find(Node->TypeName);
        if (Schema->Opcode == EGraphIROpcode::EntryEvent)
            Result.IR.EntryInstructions.push_back(NodeIndices.at(Node->Id));
        FGraphIRInstruction Instruction;
        Instruction.NodeId = Node->Id;
        Instruction.DisplayName = Node->DisplayName;
        Instruction.Opcode = Schema->Opcode;
        for (const FGraphPinSchema& PinSchema : Schema->Pins)
        {
            const FGraphPin* Pin = FindNodePin(*Node, PinSchema);
            if (Pin->Type == EGraphValueType::Exec)
            {
                if (Pin->Direction == EGraphPinDirection::Output)
                {
                    for (const FGraphLink& Link : Graph.Links)
                    {
                        if (Link.OutputPinId != Pin->Id) continue;
                        const auto Target = Owners.find(Link.InputPinId);
                        if (Target != Owners.end())
                            Instruction.ExecTargets.push_back(FGraphIRExecTarget {
                                Pin->Name, NodeIndices.at(Target->second.Node->Id)});
                    }
                }
                continue;
            }
            FGraphIROperand Operand;
            Operand.PinId = Pin->Id;
            Operand.PinName = Pin->Name;
            Operand.Direction = Pin->Direction;
            Operand.Type = Pin->Type;
            Operand.Value = Pin->DefaultValue;
            if (Pin->Direction == EGraphPinDirection::Input)
            {
                const auto Link = std::find_if(Graph.Links.begin(), Graph.Links.end(),
                    [Pin](const FGraphLink& Value) { return Value.InputPinId == Pin->Id; });
                if (Link != Graph.Links.end())
                {
                    const auto Source = Owners.find(Link->OutputPinId);
                    Operand.Source = EGraphIRValueSource::LinkedPin;
                    Operand.Value.clear();
                    Operand.SourceNodeId = Source->second.Node->Id;
                    Operand.SourcePinId = Source->second.Pin->Id;
                }
            }
            Instruction.Operands.push_back(std::move(Operand));
        }
        Result.IR.Instructions.push_back(std::move(Instruction));
    }
    Result.Bytecode = EncodeBytecode(Result.IR);
    Result.bSucceeded = true;
    return Result;
}

bool SaveGraphBytecodeToFile(
    const std::filesystem::path& FilePath,
    const FPicoGraphBytecode& Bytecode,
    std::string* OutError)
{
    if (Bytecode.Version != PicoGraphBytecodeVersion || Bytecode.Bytes.empty())
    {
        if (OutError) *OutError = "Bytecode is empty or has an unsupported version";
        return false;
    }
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    const std::filesystem::path Temp = FilePath.string() + ".tmp";
    std::ofstream Stream(Temp, std::ios::binary | std::ios::trunc);
    if (!Stream)
    {
        if (OutError) *OutError = "Could not open cooked script for writing";
        return false;
    }
    Stream.write(reinterpret_cast<const char*>(Bytecode.Bytes.data()),
        static_cast<std::streamsize>(Bytecode.Bytes.size()));
    Stream.close();
    if (!Stream)
    {
        std::filesystem::remove(Temp, Error);
        if (OutError) *OutError = "Could not write cooked script";
        return false;
    }
    std::filesystem::remove(FilePath, Error);
    Error.clear();
    std::filesystem::rename(Temp, FilePath, Error);
    if (Error)
    {
        std::filesystem::remove(Temp, Error);
        if (OutError) *OutError = "Could not commit cooked script";
        return false;
    }
    return true;
}

bool LoadGraphBytecodeFromFile(
    const std::filesystem::path& FilePath,
    FPicoGraphBytecode& OutBytecode,
    std::string* OutError)
{
    std::ifstream Stream(FilePath, std::ios::binary);
    if (!Stream)
    {
        if (OutError) *OutError = "Could not open cooked script";
        return false;
    }
    std::istreambuf_iterator<char> Begin(Stream);
    std::istreambuf_iterator<char> End;
    std::vector<std::uint8_t> Bytes(Begin, End);
    FPicoGraphBytecode Bytecode;
    Bytecode.Version = PicoGraphBytecodeVersion;
    Bytecode.Bytes = std::move(Bytes);
    FPicoGraphIR Program;
    if (!DecodeGraphBytecode(Bytecode, Program, OutError)) return false;
    Bytecode.SourceGraphId = Program.GraphId;
    OutBytecode = std::move(Bytecode);
    return true;
}

bool CookGraphAsset(
    const std::filesystem::path& SourceFile,
    const std::filesystem::path& OutputFile,
    std::string* OutError)
{
    FPicoGraphAsset Graph;
    EGraphAssetError AssetError = EGraphAssetError::None;
    if (!LoadGraphAssetFromFile(SourceFile, Graph, &AssetError))
    {
        if (OutError) *OutError = "Could not load Graph: " + std::string(ToString(AssetError));
        return false;
    }
    const FGraphCompileResult Compile = CompileGraph(Graph);
    if (!Compile.bSucceeded)
    {
        if (OutError)
        {
            *OutError = "Graph compilation failed";
            for (const FGraphDiagnostic& Diagnostic : Compile.Diagnostics)
                if (Diagnostic.Severity == EGraphDiagnosticSeverity::Error)
                    *OutError += ": " + Diagnostic.Message;
        }
        return false;
    }
    return SaveGraphBytecodeToFile(OutputFile, Compile.Bytecode, OutError);
}

std::string_view ToString(EGraphDiagnosticSeverity Severity)
{
    switch (Severity)
    {
    case EGraphDiagnosticSeverity::Info: return "Info";
    case EGraphDiagnosticSeverity::Warning: return "Warning";
    case EGraphDiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}

std::string_view ToString(EGraphDiagnosticCode Code)
{
    switch (Code)
    {
    case EGraphDiagnosticCode::InvalidAsset: return "InvalidAsset";
    case EGraphDiagnosticCode::UnknownNodeType: return "UnknownNodeType";
    case EGraphDiagnosticCode::InvalidNodePins: return "InvalidNodePins";
    case EGraphDiagnosticCode::DuplicateVariableName: return "DuplicateVariableName";
    case EGraphDiagnosticCode::InvalidDefaultValue: return "InvalidDefaultValue";
    case EGraphDiagnosticCode::MissingEntryEvent: return "MissingEntryEvent";
    case EGraphDiagnosticCode::ControlFlowCycle: return "ControlFlowCycle";
    case EGraphDiagnosticCode::UnreachableNode: return "UnreachableNode";
    }
    return "Unknown";
}

std::string_view ToString(EGraphIROpcode Opcode)
{
    switch (Opcode)
    {
    case EGraphIROpcode::EntryEvent: return "EntryEvent";
    case EGraphIROpcode::Sequence: return "Sequence";
    case EGraphIROpcode::Branch: return "Branch";
    case EGraphIROpcode::BoolLiteral: return "BoolLiteral";
    case EGraphIROpcode::FloatLiteral: return "FloatLiteral";
    case EGraphIROpcode::AddFloat: return "AddFloat";
    case EGraphIROpcode::CallFunction: return "CallFunction";
    case EGraphIROpcode::GetProperty: return "GetProperty";
    case EGraphIROpcode::SetProperty: return "SetProperty";
    case EGraphIROpcode::BroadcastDelegate: return "BroadcastDelegate";
    case EGraphIROpcode::Delay: return "Delay";
    case EGraphIROpcode::WaitGameplayEvent: return "WaitGameplayEvent";
    case EGraphIROpcode::PlayMontageAndWait: return "PlayMontageAndWait";
    case EGraphIROpcode::ActivateAbility: return "ActivateAbility";
    case EGraphIROpcode::GetBoolProperty: return "GetBoolProperty";
    case EGraphIROpcode::GetFloatProperty: return "GetFloatProperty";
    case EGraphIROpcode::PrintString: return "PrintString";
    }
    return "Unknown";
}
}
