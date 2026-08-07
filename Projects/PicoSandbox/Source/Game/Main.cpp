#include "Pico/Launch/GameLaunch.h"
#include "Pico/Engine/GameModule.h"
#include "PicoSandbox/SandboxModule.h"

#include <filesystem>
#include <memory>

#ifndef PICO_SANDBOX_PROJECT_FILE
#define PICO_SANDBOX_PROJECT_FILE ""
#endif

int main(int Argc, char** Argv)
{
    std::unique_ptr<Pico::IGameModule> GameModule =
        PicoSandbox::CreateSandboxGameModule();
    return Pico::RunPicoGame(
        Argc,
        Argv,
        GameModule.get(),
        std::filesystem::path(PICO_SANDBOX_PROJECT_FILE));
}
