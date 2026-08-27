#pragma once

#include "Pico/Graph/ScriptVM.h"

#include <functional>
#include <string>

namespace Pico
{
class PObject;

using FScriptLatentCompletion = std::function<void(bool, std::string)>;
using FScriptLatentCancel = std::function<void()>;
using FScriptLatentHandler = std::function<bool(
    PObject*,
    const FScriptExecutionReport&,
    FScriptLatentCompletion,
    FScriptLatentCancel&)>;
using FScriptAbilityActivator = std::function<bool(PObject*, int32)>;

bool RegisterScriptLatentHandler(
    EScriptLatentAction Action,
    FScriptLatentHandler Handler);
bool StartScriptLatentAction(
    PObject* Self,
    const FScriptExecutionReport& Request,
    FScriptLatentCompletion Completion,
    FScriptLatentCancel& OutCancel);
bool RegisterScriptAbilityActivator(FScriptAbilityActivator Activator);
bool ActivateScriptAbility(PObject* Self, int32 AbilityHandle);
}
