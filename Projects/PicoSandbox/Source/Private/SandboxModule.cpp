#include "PicoSandbox/SandboxModule.h"

#include "Pico/Engine/GameModule.h"
#include "PicoSandbox/SandboxGameInstance.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxEntity.h"
#include "PicoSandbox/SandboxPawn.h"

#include <memory>

namespace PicoSandbox
{
namespace
{
class FPicoSandboxGameModule final : public Pico::IGameModule
{
public:
    bool StartupModule() override
    {
        return RegisterSandboxClasses() && PSandboxPawn::RegisterClass();
    }

    std::unique_ptr<Pico::FGameInstance> CreateGameInstance() override
    {
        return std::make_unique<FSandboxGameInstance>();
    }

    void ShutdownModule() override
    {
    }
};
}

bool RegisterSandboxClasses()
{
    return PSandboxEntity::RegisterClass()
        && PSandboxCharacter::RegisterClass();
}

std::unique_ptr<Pico::IGameModule> CreateSandboxGameModule()
{
    return std::make_unique<FPicoSandboxGameModule>();
}
}
