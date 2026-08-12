#pragma once

#include <memory>

namespace Pico
{
class IGameModule;
}

namespace PicoSandbox
{
bool RegisterSandboxClasses();
bool RegisterSandboxGameplayClasses();
std::unique_ptr<Pico::IGameModule> CreateSandboxGameModule();
}
