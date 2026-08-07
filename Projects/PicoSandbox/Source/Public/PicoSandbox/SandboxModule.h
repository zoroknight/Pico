#pragma once

#include <memory>

namespace Pico
{
class IGameModule;
}

namespace PicoSandbox
{
bool RegisterSandboxClasses();
std::unique_ptr<Pico::IGameModule> CreateSandboxGameModule();
}
