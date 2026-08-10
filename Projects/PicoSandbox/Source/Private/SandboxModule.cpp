#include "PicoSandbox/SandboxModule.h"

#include "Pico/Engine/GameModule.h"
#include "PicoSandbox/SandboxGameInstance.h"
#include "PicoSandbox/SandboxGameMode.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxEntity.h"
#include "PicoSandbox/SandboxPawn.h"
#include "PicoSandbox/SandboxPlayerController.h"
#include "Pico/Object/ObjectGlobals.h"

namespace PicoSandbox
{
namespace
{
class FPicoSandboxGameModule final : public Pico::IGameModule
{
public:
    bool StartupModule() override
    {
        if (!(RegisterSandboxClasses()
            && PSandboxPawn::RegisterClass()
            && PSandboxPlayerController::RegisterClass()
            && PSandboxGameMode::RegisterClass()
            && PSandboxGameInstance::RegisterClass()))
        {
            return false;
        }
        Pico::PGameModeBase* Defaults =
            Pico::GetMutableDefault<PSandboxGameMode>();
        return Defaults != nullptr
            && Defaults->SetDefaultPawnClass(PSandboxPawn::StaticClass())
            && Defaults->SetPlayerControllerClass(
                PSandboxPlayerController::StaticClass());
    }

    const Pico::PClass* GetGameInstanceClass() const override
    {
        return PSandboxGameInstance::StaticClass();
    }

    const Pico::PClass* GetGameModeClass() const override
    {
        return PSandboxGameMode::StaticClass();
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
