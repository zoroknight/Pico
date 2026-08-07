#include "Pico/Engine/GameEngine.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModule.h"

#include <string>

namespace Pico
{
FGameEngine::FGameEngine(IGameModule* InGameModule)
    : GameModule(InGameModule)
{
}

FGameEngine::~FGameEngine() = default;

int FGameEngine::PreInit(
    int Argc,
    char** Argv,
    const std::filesystem::path& ProjectFile)
{
    const int Result = EngineLoop.PreInit(Argc, Argv, ProjectFile);
    bPreInitialized = Result == 0;
    return Result;
}

int FGameEngine::Init()
{
    if (!bPreInitialized || bInitialized)
    {
        PICO_LOG(LogEngine, Error, "Game Init called in an invalid state");
        return 1;
    }
    if (!FPaths::HasProject())
    {
        PICO_LOG(LogEngine, Error, "Game Init requires a .pico project");
        return 1;
    }

    FConfigFile Config;
    const std::filesystem::path ConfigPath =
        FPaths::GetProjectConfigFile("Pico.ini");
    if (!Config.Load(ConfigPath))
    {
        PICO_LOG(LogConfig, Error, "Game Init could not load {}", ConfigPath.string());
        return 1;
    }
    InputSystem.LoadMappings(Config);

    const std::string DefaultMap = FCommandLine::GetValue("map").value_or(
        Config.GetString("Game", "DefaultMap", ""));
    if (!ResolveContentPath(
            FPaths::GetProjectContentDir(), DefaultMap, DefaultMapPath))
    {
        PICO_LOG(
            LogEngine,
            Error,
            "Game Init has an invalid [Game] DefaultMap '{}'",
            DefaultMap);
        return 1;
    }

    int Result = EngineLoop.Init();
    if (Result != 0)
    {
        return Result;
    }

    if (GameModule != nullptr)
    {
        if (!GameModule->StartupModule())
        {
            PICO_LOG(LogEngine, Error, "Game Init failed to start the project game module");
            return 1;
        }
        bModuleStarted = true;
    }

    EWorldSerializationError WorldError = EWorldSerializationError::None;
    if (!EngineLoop.LoadWorld(DefaultMapPath, &WorldError))
    {
        PICO_LOG(
            LogEngine,
            Error,
            "Game Init failed to load default map '{}' (error={})",
            DefaultMapPath.string(),
            static_cast<int>(WorldError));
        return 1;
    }

    GameInstance = GameModule != nullptr
        ? GameModule->CreateGameInstance()
        : std::make_unique<FGameInstance>();
    if (GameInstance == nullptr || !GameInstance->Init(*this))
    {
        PICO_LOG(LogEngine, Error, "Game Init failed to initialize the game instance");
        if (GameInstance != nullptr)
        {
            GameInstance->Shutdown();
        }
        GameInstance.reset();
        return 1;
    }

    bInitialized = true;
    PICO_LOG(LogEngine, Info, "Game Init: default map={}", DefaultMapPath.string());
    return 0;
}

void FGameEngine::Tick()
{
    EngineLoop.Tick();
    if (GameInstance != nullptr)
    {
        GameInstance->Tick(EngineLoop.GetDeltaSeconds());
    }
}

void FGameEngine::Exit()
{
    if (GameInstance != nullptr)
    {
        GameInstance->Shutdown();
        GameInstance.reset();
    }
    if (bModuleStarted && GameModule != nullptr)
    {
        GameModule->ShutdownModule();
    }
    bModuleStarted = false;
    bInitialized = false;
    bPreInitialized = false;
    DefaultMapPath.clear();
    EngineLoop.Exit();
}

bool FGameEngine::ShouldExit() const
{
    return EngineLoop.ShouldExit();
}

FEngineLoop& FGameEngine::GetEngineLoop()
{
    return EngineLoop;
}

const FEngineLoop& FGameEngine::GetEngineLoop() const
{
    return EngineLoop;
}

FInputSystem& FGameEngine::GetInputSystem()
{
    return InputSystem;
}

const FInputSystem& FGameEngine::GetInputSystem() const
{
    return InputSystem;
}

FGameInstance* FGameEngine::GetGameInstance() const
{
    return GameInstance.get();
}

IGameModule* FGameEngine::GetGameModule() const
{
    return GameModule;
}

const std::filesystem::path& FGameEngine::GetDefaultMapPath() const
{
    return DefaultMapPath;
}

bool FGameEngine::ResolveContentPath(
    const std::filesystem::path& ContentRoot,
    std::string_view VirtualPath,
    std::filesystem::path& OutPath)
{
    OutPath.clear();
    constexpr std::string_view GamePrefix = "/Game/";
    if (ContentRoot.empty()
        || !VirtualPath.starts_with(GamePrefix)
        || VirtualPath.size() <= GamePrefix.size())
    {
        return false;
    }

    const std::filesystem::path RelativePath(
        std::string(VirtualPath.substr(GamePrefix.size())));
    if (RelativePath.is_absolute() || RelativePath.extension() != ".pworld")
    {
        return false;
    }

    const std::filesystem::path NormalRoot =
        std::filesystem::absolute(ContentRoot).lexically_normal();
    const std::filesystem::path Candidate =
        (NormalRoot / RelativePath).lexically_normal();
    const std::filesystem::path RelativeToRoot =
        Candidate.lexically_relative(NormalRoot);
    if (RelativeToRoot.empty()
        || RelativeToRoot == "."
        || *RelativeToRoot.begin() == "..")
    {
        return false;
    }

    std::error_code Error;
    if (!std::filesystem::is_regular_file(Candidate, Error))
    {
        return false;
    }
    OutPath = Candidate;
    return true;
}
}
