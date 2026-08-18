#include "PicoSandbox/SandboxModule.h"

#include "Pico/Engine/GameModule.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Name.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "PicoSandbox/SandboxGameInstance.h"
#include "PicoSandbox/SandboxGameMode.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxEntity.h"
#include "PicoSandbox/SandboxPawn.h"
#include "PicoSandbox/SandboxPlayerController.h"
#include "PicoSandbox/SandboxReplicationLabActor.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"

namespace PicoSandbox
{
namespace
{
class FPicoSandboxGameModule final : public Pico::IGameModule
{
public:
    bool StartupModule() override
    {
        return RegisterSandboxGameplayClasses();
    }

    bool PostActorBlueprintCompile() override
    {
        Pico::FConfigFile Config;
        Config.Load(Pico::FPaths::GetProjectConfigFile("Pico.ini"));
        const auto ResolveClass = [&Config](
            const char* Key,
            const Pico::PClass* Fallback,
            const Pico::PClass* RequiredBase)
        {
            const std::string Name = Config.GetString("Game", Key, "");
            const Pico::PClass* Class = Name.empty()
                ? Fallback
                : Pico::FClassRegistry::FindClass(Pico::FName(Name));
            return Class != nullptr && Class->IsChildOf(RequiredBase) && Class->CanConstruct()
                ? Class : Fallback;
        };
        Pico::PGameModeBase* Defaults =
            Pico::GetMutableDefault<PSandboxGameMode>();
        return Defaults != nullptr
            && Defaults->SetDefaultPawnClass(ResolveClass(
                "DefaultPawnClass", PSandboxPawn::StaticClass(), Pico::PPawn::StaticClass()))
            && Defaults->SetPlayerControllerClass(
                ResolveClass("PlayerControllerClass", PSandboxPlayerController::StaticClass(),
                    Pico::PPlayerController::StaticClass()));
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

bool RegisterSandboxGameplayClasses()
{
    return RegisterSandboxClasses()
        && PSandboxPawn::RegisterClass()
        && PSandboxReplicationLabActor::RegisterClass()
        && PSandboxPlayerController::RegisterClass()
        && PSandboxGameMode::RegisterClass()
        && PSandboxGameInstance::RegisterClass();
}

std::unique_ptr<Pico::IGameModule> CreateSandboxGameModule()
{
    return std::make_unique<FPicoSandboxGameModule>();
}
}
