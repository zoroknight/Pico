#pragma once

#include "Pico/Graph/GraphAsset.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
inline constexpr std::uint32_t PicoGraphBytecodeVersion = 3;

enum class EGraphDiagnosticSeverity
{
    Info,
    Warning,
    Error
};

enum class EGraphDiagnosticCode
{
    InvalidAsset,
    UnknownNodeType,
    InvalidNodePins,
    DuplicateVariableName,
    InvalidDefaultValue,
    MissingEntryEvent,
    ControlFlowCycle,
    UnreachableNode
};

struct FGraphDiagnostic
{
    EGraphDiagnosticSeverity Severity = EGraphDiagnosticSeverity::Error;
    EGraphDiagnosticCode Code = EGraphDiagnosticCode::InvalidAsset;
    std::string Message;
    std::string NodeId;
    std::string PinId;
};

struct FGraphPinSchema
{
    std::string Name;
    EGraphPinDirection Direction = EGraphPinDirection::Input;
    EGraphValueType Type = EGraphValueType::Exec;
    std::string DefaultValue;
};

enum class EGraphIROpcode : std::uint8_t
{
    EntryEvent,
    Sequence,
    Branch,
    BoolLiteral,
    FloatLiteral,
    AddFloat,
    CallFunction,
    GetProperty,
    SetProperty,
    BroadcastDelegate,
    Delay,
    WaitGameplayEvent,
    PlayMontageAndWait,
    ActivateAbility,
    GetBoolProperty,
    GetFloatProperty,
    PrintString
};

struct FGraphNodeSchema
{
    std::string TypeName;
    std::string DisplayName;
    EGraphIROpcode Opcode = EGraphIROpcode::Sequence;
    std::vector<FGraphPinSchema> Pins;
};

class FGraphSchemaRegistry
{
public:
    const FGraphNodeSchema* Find(std::string_view TypeName) const;
    const std::vector<FGraphNodeSchema>& GetSchemas() const;

private:
    friend const FGraphSchemaRegistry& GetDefaultGraphSchemaRegistry();
    std::vector<FGraphNodeSchema> Schemas;
};

const FGraphSchemaRegistry& GetDefaultGraphSchemaRegistry();
bool MakeSchemaGraphNode(
    std::string_view TypeName,
    float X,
    float Y,
    FGraphNode& OutNode);

enum class EGraphIRValueSource : std::uint8_t
{
    DefaultValue,
    LinkedPin
};

struct FGraphIROperand
{
    std::string PinId;
    std::string PinName;
    EGraphPinDirection Direction = EGraphPinDirection::Input;
    EGraphValueType Type = EGraphValueType::Exec;
    EGraphIRValueSource Source = EGraphIRValueSource::DefaultValue;
    std::string Value;
    std::string SourceNodeId;
    std::string SourcePinId;
};

struct FGraphIRExecTarget
{
    std::string PinName;
    std::uint32_t InstructionIndex = 0;
};

struct FGraphIRInstruction
{
    std::string NodeId;
    std::string DisplayName;
    EGraphIROpcode Opcode = EGraphIROpcode::Sequence;
    std::vector<FGraphIRExecTarget> ExecTargets;
    std::vector<FGraphIROperand> Operands;
};

struct FGraphIRVariable
{
    std::string Id;
    std::string Name;
    EGraphValueType Type = EGraphValueType::Float;
    std::string DefaultValue;
};

struct FPicoGraphIR
{
    std::string GraphId;
    std::vector<FGraphIRVariable> Variables;
    std::vector<std::uint32_t> EntryInstructions;
    std::vector<FGraphIRInstruction> Instructions;
};

struct FPicoGraphBytecode
{
    std::uint32_t Version = PicoGraphBytecodeVersion;
    std::string SourceGraphId;
    std::vector<std::uint8_t> Bytes;
};

struct FGraphCompileResult
{
    bool bSucceeded = false;
    std::vector<FGraphDiagnostic> Diagnostics;
    FPicoGraphIR IR;
    FPicoGraphBytecode Bytecode;
};

std::vector<FGraphDiagnostic> ValidateGraphSemantics(
    const FPicoGraphAsset& Graph,
    const FGraphSchemaRegistry& Registry = GetDefaultGraphSchemaRegistry());
FGraphCompileResult CompileGraph(
    const FPicoGraphAsset& Graph,
    const FGraphSchemaRegistry& Registry = GetDefaultGraphSchemaRegistry());
bool SaveGraphBytecodeToFile(
    const std::filesystem::path& FilePath,
    const FPicoGraphBytecode& Bytecode,
    std::string* OutError = nullptr);
bool LoadGraphBytecodeFromFile(
    const std::filesystem::path& FilePath,
    FPicoGraphBytecode& OutBytecode,
    std::string* OutError = nullptr);
bool CookGraphAsset(
    const std::filesystem::path& SourceFile,
    const std::filesystem::path& OutputFile,
    std::string* OutError = nullptr);

std::string_view ToString(EGraphDiagnosticSeverity Severity);
std::string_view ToString(EGraphDiagnosticCode Code);
std::string_view ToString(EGraphIROpcode Opcode);
}
