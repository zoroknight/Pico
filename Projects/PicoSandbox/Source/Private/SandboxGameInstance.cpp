#include "PicoSandbox/SandboxGameInstance.h"

#include "Pico/Engine/GameEngine.h"

namespace PicoSandbox
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxGameInstance)

PSandboxGameInstance::PSandboxGameInstance(
    const Pico::FObjectConstructionParams& Params)
    : PGameInstance(Params)
{
}

bool PSandboxGameInstance::Init(Pico::FGameEngine& GameEngine)
{
    return Pico::PGameInstance::Init(GameEngine);
}

}
