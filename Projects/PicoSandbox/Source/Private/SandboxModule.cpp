#include "PicoSandbox/SandboxModule.h"

#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxEntity.h"

namespace PicoSandbox
{
bool RegisterSandboxClasses()
{
    return PSandboxEntity::RegisterClass()
        && PSandboxCharacter::RegisterClass();
}
}
