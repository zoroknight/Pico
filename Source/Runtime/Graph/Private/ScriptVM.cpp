#include "Pico/Graph/ScriptVM.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/DynamicMulticastDelegate.h"
#include "Pico/Object/Function.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Property.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Pico
{
namespace
{
class FBytecodeReader
{
public:
    explicit FBytecodeReader(const std::vector<std::uint8_t>& InBytes) : Bytes(InBytes) {}

    bool ReadU8(std::uint8_t& Value)
    {
        if (Offset >= Bytes.size()) return false;
        Value = Bytes[Offset++];
        return true;
    }

    bool ReadU32(std::uint32_t& Value)
    {
        if (Bytes.size() - Offset < 4) return false;
        Value = 0;
        for (int Shift = 0; Shift < 32; Shift += 8)
            Value |= static_cast<std::uint32_t>(Bytes[Offset++]) << Shift;
        return true;
    }

    bool ReadString(std::string& Value)
    {
        std::uint32_t Size = 0;
        if (!ReadU32(Size) || Size > 1024 * 1024 || Bytes.size() - Offset < Size)
            return false;
        Value.assign(reinterpret_cast<const char*>(Bytes.data() + Offset), Size);
        Offset += Size;
        return true;
    }

    bool AtEnd() const { return Offset == Bytes.size(); }

private:
    const std::vector<std::uint8_t>& Bytes;
    std::size_t Offset = 0;
};

const FGraphIROperand* FindOperand(
    const FGraphIRInstruction& Instruction,
    std::string_view Name)
{
    const auto Found = std::find_if(
        Instruction.Operands.begin(), Instruction.Operands.end(),
        [Name](const FGraphIROperand& Operand) { return Operand.PinName == Name; });
    return Found == Instruction.Operands.end() ? nullptr : &*Found;
}

bool ParseBool(std::string_view Text, bool& Value)
{
    if (Text == "true") { Value = true; return true; }
    if (Text == "false") { Value = false; return true; }
    return false;
}

bool ParseFloat(std::string_view Text, float& Value)
{
    std::string Copy(Text);
    char* End = nullptr;
    Value = std::strtof(Copy.c_str(), &End);
    return End == Copy.c_str() + Copy.size() && std::isfinite(Value);
}

bool ParseInt(std::string_view Text, int32& Value)
{
    const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
    return Result.ec == std::errc {} && Result.ptr == Text.data() + Text.size();
}

bool ParseVector(std::string_view Text, FVector3& Value)
{
    std::string Copy(Text);
    std::replace(Copy.begin(), Copy.end(), ',', ' ');
    std::istringstream Stream(Copy);
    std::string Extra;
    return (Stream >> Value.X >> Value.Y >> Value.Z) && !(Stream >> Extra);
}

std::string PropertyToString(const PProperty& Property, const PObject* Object)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0; return Property.GetValue(Object, Value) ? std::to_string(Value) : std::string {};
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f; return Property.GetValue(Object, Value) ? std::to_string(Value) : std::string {};
    }
    case EPropertyType::Bool:
    {
        bool Value = false; return Property.GetValue(Object, Value) ? (Value ? "true" : "false") : std::string {};
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (!Property.GetValue(Object, Value)) return {};
        return std::to_string(Value.X) + "," + std::to_string(Value.Y) + "," + std::to_string(Value.Z);
    }
    default:
        return {};
    }
}

bool SetPropertyFromString(PProperty const& Property, PObject* Object, std::string_view Text)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0; return ParseInt(Text, Value) && Property.SetValue(Object, Value);
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f; return ParseFloat(Text, Value) && Property.SetValue(Object, Value);
    }
    case EPropertyType::Bool:
    {
        bool Value = false; return ParseBool(Text, Value) && Property.SetValue(Object, Value);
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value; return ParseVector(Text, Value) && Property.SetValue(Object, Value);
    }
    default:
        return false;
    }
}

struct FVMState
{
    const FPicoGraphIR& Program;
    const FScriptExecutionContext& Context;
    FScriptExecutionReport Report;
    std::unordered_map<std::string, std::uint32_t> NodeIndices;
    std::vector<std::size_t> Visits;
};

void Fail(FVMState& State, EScriptExecutionResult Result, std::string Message)
{
    if (State.Report.Result == EScriptExecutionResult::Success)
    {
        State.Report.Result = Result;
        State.Report.Message = std::move(Message);
    }
}

bool ConsumeInstruction(FVMState& State, std::uint32_t Index)
{
    if (Index >= State.Program.Instructions.size())
    {
        Fail(State, EScriptExecutionResult::InvalidInstruction, "Instruction index is out of range");
        return false;
    }
    if (++State.Report.InstructionsExecuted > State.Context.Limits.MaxInstructions)
    {
        Fail(State, EScriptExecutionResult::InstructionBudgetExceeded, "Instruction budget exceeded");
        return false;
    }
    if (++State.Visits[Index] > State.Context.Limits.MaxLoopIterations)
    {
        Fail(State, EScriptExecutionResult::LoopBudgetExceeded, "Per-instruction loop budget exceeded");
        return false;
    }
    return true;
}

bool EvaluateOutput(
    FVMState& State,
    std::uint32_t InstructionIndex,
    std::string_view PinId,
    std::size_t Depth,
    FFunctionValue& OutValue);

bool EvaluateOperand(
    FVMState& State,
    const FGraphIROperand& Operand,
    std::size_t Depth,
    FFunctionValue& OutValue)
{
    State.Report.MaximumCallDepth = std::max(State.Report.MaximumCallDepth, Depth);
    if (Depth > State.Context.Limits.MaxCallDepth)
    {
        Fail(State, EScriptExecutionResult::CallDepthExceeded, "Data evaluation call-depth budget exceeded");
        return false;
    }
    if (Operand.Source == EGraphIRValueSource::LinkedPin)
    {
        const auto Found = State.NodeIndices.find(Operand.SourceNodeId);
        return Found != State.NodeIndices.end()
            && EvaluateOutput(State, Found->second, Operand.SourcePinId, Depth + 1, OutValue);
    }
    switch (Operand.Type)
    {
    case EGraphValueType::Bool:
    {
        bool Value = false; if (!ParseBool(Operand.Value, Value)) return false; OutValue = Value; return true;
    }
    case EGraphValueType::Int:
    {
        int32 Value = 0; if (!ParseInt(Operand.Value, Value)) return false; OutValue = Value; return true;
    }
    case EGraphValueType::Float:
    {
        float Value = 0.0f; if (!ParseFloat(Operand.Value, Value)) return false; OutValue = Value; return true;
    }
    case EGraphValueType::String: OutValue = Operand.Value; return true;
    case EGraphValueType::Vector:
    {
        FVector3 Value; if (!ParseVector(Operand.Value, Value)) return false; OutValue = Value; return true;
    }
    case EGraphValueType::Object: OutValue = State.Context.Self; return true;
    case EGraphValueType::Exec: return false;
    }
    return false;
}

bool EvaluateOutput(
    FVMState& State,
    std::uint32_t InstructionIndex,
    std::string_view PinId,
    std::size_t Depth,
    FFunctionValue& OutValue)
{
    if (!ConsumeInstruction(State, InstructionIndex)) return false;
    const FGraphIRInstruction& Instruction = State.Program.Instructions[InstructionIndex];
    const auto Output = std::find_if(
        Instruction.Operands.begin(), Instruction.Operands.end(),
        [PinId](const FGraphIROperand& Operand)
        {
            return Operand.Direction == EGraphPinDirection::Output && Operand.PinId == PinId;
        });
    if (Output == Instruction.Operands.end()) return false;
    switch (Instruction.Opcode)
    {
    case EGraphIROpcode::BoolLiteral:
    case EGraphIROpcode::FloatLiteral:
        return EvaluateOperand(State, *Output, Depth, OutValue);
    case EGraphIROpcode::AddFloat:
    {
        const FGraphIROperand* A = FindOperand(Instruction, "A");
        const FGraphIROperand* B = FindOperand(Instruction, "B");
        FFunctionValue Left;
        FFunctionValue Right;
        if (A == nullptr || B == nullptr
            || !EvaluateOperand(State, *A, Depth + 1, Left)
            || !EvaluateOperand(State, *B, Depth + 1, Right)
            || !std::holds_alternative<float>(Left)
            || !std::holds_alternative<float>(Right)) return false;
        OutValue = std::get<float>(Left) + std::get<float>(Right);
        return true;
    }
    case EGraphIROpcode::GetProperty:
    {
        if (State.Context.Self == nullptr) return false;
        const FGraphIROperand* Name = FindOperand(Instruction, "PropertyName");
        FFunctionValue NameValue;
        if (Name == nullptr || !EvaluateOperand(State, *Name, Depth + 1, NameValue)
            || !std::holds_alternative<std::string>(NameValue)) return false;
        const PProperty* Property = State.Context.Self->GetClass()->FindProperty(
            FName(std::get<std::string>(NameValue)));
        if (Property == nullptr) return false;
        OutValue = PropertyToString(*Property, State.Context.Self);
        return true;
    }
    default:
        return false;
    }
}

const FGraphIRExecTarget* FindExecTarget(
    const FGraphIRInstruction& Instruction,
    std::string_view PinName)
{
    const auto Found = std::find_if(
        Instruction.ExecTargets.begin(), Instruction.ExecTargets.end(),
        [PinName](const FGraphIRExecTarget& Target) { return Target.PinName == PinName; });
    return Found == Instruction.ExecTargets.end() ? nullptr : &*Found;
}
}

bool DecodeGraphBytecode(
    const FPicoGraphBytecode& Bytecode,
    FPicoGraphIR& OutProgram,
    std::string* OutError)
{
    auto Error = [OutError](std::string Message)
    {
        if (OutError != nullptr) *OutError = std::move(Message);
        return false;
    };
    FBytecodeReader Reader(Bytecode.Bytes);
    std::uint8_t Magic[4] {};
    for (std::uint8_t& Value : Magic) if (!Reader.ReadU8(Value)) return Error("Truncated bytecode header");
    if (Magic[0] != 'P' || Magic[1] != 'G' || Magic[2] != 'R' || Magic[3] != 'B')
        return Error("Invalid PGRB magic");
    std::uint32_t Version = 0;
    if (!Reader.ReadU32(Version) || Version != PicoGraphBytecodeVersion || Version != Bytecode.Version)
        return Error("Unsupported PGRB version");

    FPicoGraphIR Program;
    if (!Reader.ReadString(Program.GraphId)) return Error("Invalid graph id");
    std::uint32_t Count = 0;
    if (!Reader.ReadU32(Count) || Count > 100000) return Error("Invalid variable count");
    Program.Variables.resize(Count);
    for (FGraphIRVariable& Variable : Program.Variables)
    {
        std::uint8_t Type = 0;
        if (!Reader.ReadString(Variable.Id) || !Reader.ReadString(Variable.Name)
            || !Reader.ReadU8(Type) || !Reader.ReadString(Variable.DefaultValue))
            return Error("Invalid variable record");
        if (Type > static_cast<std::uint8_t>(EGraphValueType::Object))
            return Error("Invalid variable type");
        Variable.Type = static_cast<EGraphValueType>(Type);
    }
    if (!Reader.ReadU32(Count) || Count > 100000) return Error("Invalid entry count");
    Program.EntryInstructions.resize(Count);
    for (std::uint32_t& Entry : Program.EntryInstructions)
        if (!Reader.ReadU32(Entry)) return Error("Invalid entry record");
    if (!Reader.ReadU32(Count) || Count > 100000) return Error("Invalid instruction count");
    Program.Instructions.resize(Count);
    for (FGraphIRInstruction& Instruction : Program.Instructions)
    {
        std::uint8_t Opcode = 0;
        if (!Reader.ReadU8(Opcode) || !Reader.ReadString(Instruction.NodeId)
            || !Reader.ReadString(Instruction.DisplayName)) return Error("Invalid instruction record");
        if (Opcode > static_cast<std::uint8_t>(EGraphIROpcode::BroadcastDelegate))
            return Error("Invalid instruction opcode");
        Instruction.Opcode = static_cast<EGraphIROpcode>(Opcode);
        std::uint32_t TargetCount = 0;
        if (!Reader.ReadU32(TargetCount) || TargetCount > Count) return Error("Invalid exec target count");
        Instruction.ExecTargets.resize(TargetCount);
        for (FGraphIRExecTarget& Target : Instruction.ExecTargets)
            if (!Reader.ReadString(Target.PinName) || !Reader.ReadU32(Target.InstructionIndex))
                return Error("Invalid exec target");
        std::uint32_t OperandCount = 0;
        if (!Reader.ReadU32(OperandCount) || OperandCount > 1024) return Error("Invalid operand count");
        Instruction.Operands.resize(OperandCount);
        for (FGraphIROperand& Operand : Instruction.Operands)
        {
            std::uint8_t Direction = 0;
            std::uint8_t Type = 0;
            std::uint8_t Source = 0;
            if (!Reader.ReadString(Operand.PinId) || !Reader.ReadString(Operand.PinName)
                || !Reader.ReadU8(Direction) || !Reader.ReadU8(Type) || !Reader.ReadU8(Source)
                || !Reader.ReadString(Operand.Value) || !Reader.ReadString(Operand.SourceNodeId)
                || !Reader.ReadString(Operand.SourcePinId)) return Error("Invalid operand record");
            if (Direction > static_cast<std::uint8_t>(EGraphPinDirection::Output)
                || Type > static_cast<std::uint8_t>(EGraphValueType::Object)
                || Source > static_cast<std::uint8_t>(EGraphIRValueSource::LinkedPin))
                return Error("Invalid operand enum value");
            Operand.Direction = static_cast<EGraphPinDirection>(Direction);
            Operand.Type = static_cast<EGraphValueType>(Type);
            Operand.Source = static_cast<EGraphIRValueSource>(Source);
        }
    }
    if (!Reader.AtEnd()) return Error("Trailing PGRB data");
    if (!Bytecode.SourceGraphId.empty() && Bytecode.SourceGraphId != Program.GraphId)
        return Error("PGRB source graph id mismatch");
    for (std::uint32_t Entry : Program.EntryInstructions)
        if (Entry >= Program.Instructions.size()) return Error("Entry index is out of range");
    for (const FGraphIRInstruction& Instruction : Program.Instructions)
        for (const FGraphIRExecTarget& Target : Instruction.ExecTargets)
            if (Target.InstructionIndex >= Program.Instructions.size())
                return Error("Exec target index is out of range");
    OutProgram = std::move(Program);
    return true;
}

FScriptExecutionReport FPicoScriptVM::Execute(
    const FPicoGraphBytecode& Bytecode,
    const FScriptExecutionContext& Context) const
{
    FPicoGraphIR Program;
    std::string Error;
    if (!DecodeGraphBytecode(Bytecode, Program, &Error))
        return {EScriptExecutionResult::InvalidProgram, std::move(Error)};
    return Execute(Program, Context);
}

FScriptExecutionReport FPicoScriptVM::Execute(
    const FPicoGraphIR& Program,
    const FScriptExecutionContext& Context) const
{
    FVMState State {Program, Context};
    State.Report.Result = EScriptExecutionResult::Success;
    State.Visits.resize(Program.Instructions.size());
    for (std::uint32_t Index = 0; Index < Program.Instructions.size(); ++Index)
        State.NodeIndices.emplace(Program.Instructions[Index].NodeId, Index);

    std::vector<std::uint32_t> Work;
    for (std::uint32_t Entry : Program.EntryInstructions)
    {
        if (Entry < Program.Instructions.size()
            && Program.Instructions[Entry].DisplayName == Context.EntryEvent)
            Work.push_back(Entry);
    }
    if (Work.empty())
        return {EScriptExecutionResult::EntryNotFound, "Entry event was not found"};

    while (!Work.empty() && State.Report.Succeeded())
    {
        const std::uint32_t Index = Work.back();
        Work.pop_back();
        if (!ConsumeInstruction(State, Index)) break;
        const FGraphIRInstruction& Instruction = Program.Instructions[Index];
        std::string NextPin = "Then";
        switch (Instruction.Opcode)
        {
        case EGraphIROpcode::EntryEvent:
        case EGraphIROpcode::Sequence:
            break;
        case EGraphIROpcode::Branch:
        {
            const FGraphIROperand* Condition = FindOperand(Instruction, "Condition");
            FFunctionValue Value;
            if (Condition == nullptr || !EvaluateOperand(State, *Condition, 1, Value)
                || !std::holds_alternative<bool>(Value))
            {
                Fail(State, EScriptExecutionResult::TypeError, "Branch condition is not Bool");
                continue;
            }
            NextPin = std::get<bool>(Value) ? "True" : "False";
            break;
        }
        case EGraphIROpcode::CallFunction:
        {
            const FGraphIROperand* Name = FindOperand(Instruction, "FunctionName");
            FFunctionValue Value;
            if (Context.Self == nullptr || Name == nullptr || !EvaluateOperand(State, *Name, 1, Value)
                || !std::holds_alternative<std::string>(Value))
            {
                Fail(State, EScriptExecutionResult::ReflectionError, "CallFunction has no valid target or name");
                continue;
            }
            const PFunction* Function = Context.Self->GetClass()->FindFunction(
                FName(std::get<std::string>(Value)));
            if (Function == nullptr || !Function->HasAnyFlags(EFunctionFlags::Callable)
                || Context.Self->ProcessEvent(Function) != EFunctionInvokeResult::Success)
                Fail(State, EScriptExecutionResult::ReflectionError, "Reflected function call failed");
            break;
        }
        case EGraphIROpcode::SetProperty:
        {
            const FGraphIROperand* Name = FindOperand(Instruction, "PropertyName");
            const FGraphIROperand* Input = FindOperand(Instruction, "Value");
            FFunctionValue NameValue;
            FFunctionValue InputValue;
            if (Context.Self == nullptr || Name == nullptr || Input == nullptr
                || !EvaluateOperand(State, *Name, 1, NameValue)
                || !EvaluateOperand(State, *Input, 1, InputValue)
                || !std::holds_alternative<std::string>(NameValue)
                || !std::holds_alternative<std::string>(InputValue))
            {
                Fail(State, EScriptExecutionResult::ReflectionError, "SetProperty arguments are invalid");
                continue;
            }
            const PProperty* Property = Context.Self->GetClass()->FindProperty(
                FName(std::get<std::string>(NameValue)));
            if (Property == nullptr
                || Property->HasAnyFlags(EPropertyFlags::ReadOnly)
                || !SetPropertyFromString(
                    *Property, Context.Self, std::get<std::string>(InputValue)))
                Fail(State, EScriptExecutionResult::ReflectionError, "Reflected property write failed");
            break;
        }
        case EGraphIROpcode::BroadcastDelegate:
        {
            const FGraphIROperand* Name = FindOperand(Instruction, "DelegateName");
            FFunctionValue Value;
            if (Context.Self == nullptr || Name == nullptr || !EvaluateOperand(State, *Name, 1, Value)
                || !std::holds_alternative<std::string>(Value))
            {
                Fail(State, EScriptExecutionResult::ReflectionError, "BroadcastDelegate has no valid target or name");
                continue;
            }
            const PProperty* Property = Context.Self->GetClass()->FindProperty(
                FName(std::get<std::string>(Value)));
            FDynamicMulticastDelegate* Delegate = Property != nullptr
                ? Property->GetDynamicMulticastDelegate(Context.Self) : nullptr;
            if (Delegate == nullptr || !Delegate->GetParameters().empty()
                || Delegate->Broadcast().Result != EDynamicDelegateBroadcastResult::Success)
                Fail(State, EScriptExecutionResult::ReflectionError, "Dynamic delegate broadcast failed");
            break;
        }
        case EGraphIROpcode::BoolLiteral:
        case EGraphIROpcode::FloatLiteral:
        case EGraphIROpcode::AddFloat:
        case EGraphIROpcode::GetProperty:
            Fail(State, EScriptExecutionResult::InvalidInstruction, "Pure node appeared in control flow");
            continue;
        }
        const FGraphIRExecTarget* Target = FindExecTarget(Instruction, NextPin);
        if (Target != nullptr) Work.push_back(Target->InstructionIndex);
    }
    if (State.Report.Succeeded()) State.Report.Message = "Graph execution completed";
    return State.Report;
}

std::string_view ToString(EScriptExecutionResult Result)
{
    switch (Result)
    {
    case EScriptExecutionResult::Success: return "Success";
    case EScriptExecutionResult::InvalidProgram: return "InvalidProgram";
    case EScriptExecutionResult::EntryNotFound: return "EntryNotFound";
    case EScriptExecutionResult::InvalidInstruction: return "InvalidInstruction";
    case EScriptExecutionResult::InstructionBudgetExceeded: return "InstructionBudgetExceeded";
    case EScriptExecutionResult::LoopBudgetExceeded: return "LoopBudgetExceeded";
    case EScriptExecutionResult::CallDepthExceeded: return "CallDepthExceeded";
    case EScriptExecutionResult::ReflectionError: return "ReflectionError";
    case EScriptExecutionResult::TypeError: return "TypeError";
    }
    return "Unknown";
}
}
