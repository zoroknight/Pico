#pragma once

#include "Pico/Engine/EngineLoop.h"
#include "Pico/Input/InputSystem.h"

#include <filesystem>
#include <memory>

namespace Pico
{
class FGameInstance;
class IGameModule;

class FGameEngine
{
public:
    explicit FGameEngine(IGameModule* InGameModule = nullptr);
    ~FGameEngine();

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
    FGameInstance* GetGameInstance() const;
    IGameModule* GetGameModule() const;
    const std::filesystem::path& GetDefaultMapPath() const;

    static bool ResolveContentPath(
        const std::filesystem::path& ContentRoot,
        std::string_view VirtualPath,
        std::filesystem::path& OutPath);

private:
    FEngineLoop EngineLoop;
    FInputSystem InputSystem;
    IGameModule* GameModule = nullptr;
    std::unique_ptr<FGameInstance> GameInstance;
    std::filesystem::path DefaultMapPath;
    bool bModuleStarted = false;
    bool bPreInitialized = false;
    bool bInitialized = false;
};
}
