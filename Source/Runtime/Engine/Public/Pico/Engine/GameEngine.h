#pragma once

#include "Pico/Engine/EngineLoop.h"
#include "Pico/Input/InputSystem.h"

#include <filesystem>

namespace Pico
{
class FGameEngine
{
public:
    int PreInit(
        int Argc,
        char** Argv,
        const std::filesystem::path& ProjectFile = {});
    int Init();
    void Tick();
    void Exit();

    bool ShouldExit() const;
    FEngineLoop& GetEngineLoop();
    const FEngineLoop& GetEngineLoop() const;
    FInputSystem& GetInputSystem();
    const FInputSystem& GetInputSystem() const;
    const std::filesystem::path& GetDefaultMapPath() const;

    static bool ResolveContentPath(
        const std::filesystem::path& ContentRoot,
        std::string_view VirtualPath,
        std::filesystem::path& OutPath);

private:
    FEngineLoop EngineLoop;
    FInputSystem InputSystem;
    std::filesystem::path DefaultMapPath;
    bool bPreInitialized = false;
    bool bInitialized = false;
};
}
