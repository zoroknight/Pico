#pragma once

#include "Pico/Core/Types.h"
#include "Pico/Graph/GraphCompiler.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
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
    TypeError,
    Suspended
};

enum class EScriptLatentAction
{
    None,
    Delay,
    WaitGameplayEvent,
    PlayMontageAndWait
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
    std::optional<std::uint32_t> StartInstruction;
    std::function<bool(int32)> ActivateAbility;
    std::function<void(std::string, float)> PrintString;
};

struct FScriptExecutionReport
{
    EScriptExecutionResult Result = EScriptExecutionResult::InvalidProgram;
    std::string Message;
    std::size_t InstructionsExecuted = 0;
    std::size_t MaximumCallDepth = 0;
    EScriptLatentAction LatentAction = EScriptLatentAction::None;
    std::uint32_t ContinuationInstruction = UINT32_MAX;
    std::uint32_t AlternateContinuationInstruction = UINT32_MAX;
    float LatentSeconds = 0.0f;
    float LatentPlayRate = 1.0f;
    int32 AbilityHandle = 0;
    bool bExactMatch = false;
    std::string LatentPayload;

    bool Succeeded() const { return Result == EScriptExecutionResult::Success; }
    bool IsSuspended() const { return Result == EScriptExecutionResult::Suspended; }
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
std::string_view ToString(EScriptLatentAction Action);
}
