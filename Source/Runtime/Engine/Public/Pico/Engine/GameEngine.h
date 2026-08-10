#pragma once

#include "Pico/Engine/EngineLoop.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/ObjectTypes.h"

#include <filesystem>

namespace Pico
{
class PGameInstance;
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
    bool LoadMap(
        const std::filesystem::path& FilePath,
        EWorldSerializationError* OutError = nullptr);

    bool ShouldExit() const;
    FEngineLoop& GetEngineLoop();
    const FEngineLoop& GetEngineLoop() const;
    FInputSystem& GetInputSystem();
    const FInputSystem& GetInputSystem() const;
    PGameInstance* GetGameInstance() const;
    IGameModule* GetGameModule() const;
    const std::filesystem::path& GetDefaultMapPath() const;

    static bool ResolveContentPath(
        const std::filesystem::path& ContentRoot,
        std::string_view VirtualPath,
        std::filesystem::path& OutPath);

private:
    bool CreateGameInstance();
    void DestroyGameInstance();

    FEngineLoop EngineLoop;
    FInputSystem InputSystem;
    IGameModule* GameModule = nullptr;
    FObjectHandle GameInstanceHandle;
    std::filesystem::path DefaultMapPath;
    bool bModuleStarted = false;
    bool bPreInitialized = false;
    bool bInitialized = false;
};
}
