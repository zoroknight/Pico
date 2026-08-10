#include "PicoSandbox/SandboxGameMode.h"

namespace PicoSandbox
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PSandboxGameMode)

PSandboxGameMode::PSandboxGameMode(
    const Pico::FObjectConstructionParams& Params)
    : PGameModeBase(Params)
{
}
}
