#pragma once

#include "Pico/Graph/GraphCompiler.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Pico
{
class PObject;

enum class EScriptExecutionResult
{
    Success,
    InvalidProgram,
    EntryNotFound,
    InvalidInstruction,
    InstructionBudgetExceeded,
    LoopBudgetExceeded,
    CallDepthExceeded,
    ReflectionError,
    TypeError
};

struct FScriptExecutionLimits
{
    std::size_t MaxInstructions = 1024;
    std::size_t MaxLoopIterations = 64;
    std::size_t MaxCallDepth = 16;
};

struct FScriptExecutionContext
{
    PObject* Self = nullptr;
    std::string EntryEvent = "BeginPlay";
    FScriptExecutionLimits Limits;
};

struct FScriptExecutionReport
{
    EScriptExecutionResult Result = EScriptExecutionResult::InvalidProgram;
    std::string Message;
    std::size_t InstructionsExecuted = 0;
    std::size_t MaximumCallDepth = 0;

    bool Succeeded() const { return Result == EScriptExecutionResult::Success; }
};

class FPicoScriptVM
{
public:
    FScriptExecutionReport Execute(
        const FPicoGraphBytecode& Bytecode,
        const FScriptExecutionContext& Context) const;
    FScriptExecutionReport Execute(
        const FPicoGraphIR& Program,
        const FScriptExecutionContext& Context) const;
};

bool DecodeGraphBytecode(
    const FPicoGraphBytecode& Bytecode,
    FPicoGraphIR& OutProgram,
    std::string* OutError = nullptr);

std::string_view ToString(EScriptExecutionResult Result);
}
