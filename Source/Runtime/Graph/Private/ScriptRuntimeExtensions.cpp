#include "Pico/Graph/ScriptRuntimeExtensions.h"

#include <map>
#include <mutex>
#include <utility>

namespace Pico
{
namespace
{
std::mutex RegistryMutex;
std::map<EScriptLatentAction, FScriptLatentHandler> LatentHandlers;
FScriptAbilityActivator AbilityActivator;
}

bool RegisterScriptLatentHandler(
    EScriptLatentAction Action,
    FScriptLatentHandler Handler)
{
    if (Action == EScriptLatentAction::None || !Handler) return false;
    std::scoped_lock Lock(RegistryMutex);
    LatentHandlers[Action] = std::move(Handler);
    return true;
}

bool StartScriptLatentAction(
    PObject* Self,
    const FScriptExecutionReport& Request,
    FScriptLatentCompletion Completion,
    FScriptLatentCancel& OutCancel)
{
    FScriptLatentHandler Handler;
    {
        std::scoped_lock Lock(RegistryMutex);
        const auto Found = LatentHandlers.find(Request.LatentAction);
        if (Found == LatentHandlers.end()) return false;
        Handler = Found->second;
    }
    return Handler(Self, Request, std::move(Completion), OutCancel);
}

bool RegisterScriptAbilityActivator(FScriptAbilityActivator Activator)
{
    if (!Activator) return false;
    std::scoped_lock Lock(RegistryMutex);
    AbilityActivator = std::move(Activator);
    return true;
}

bool ActivateScriptAbility(PObject* Self, int32 AbilityHandle)
{
    FScriptAbilityActivator Activator;
    {
        std::scoped_lock Lock(RegistryMutex);
        Activator = AbilityActivator;
    }
    return Activator && Activator(Self, AbilityHandle);
}
}
