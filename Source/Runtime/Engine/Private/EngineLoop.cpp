#include "Pico/Engine/EngineLoop.h"

#include "Pico/Core/App.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/Types.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"

#include <cmath>
#include <exception>
#include <filesystem>

#ifndef PICO_PROJECT_NAME
#define PICO_PROJECT_NAME "Pico"
#endif

#ifndef PICO_VERSION
#define PICO_VERSION "0.1.0"
#endif

namespace Pico
{
int FEngineLoop::PreInit(int Argc, char** Argv)
{
    if (bPreInitialized || bInitialized)
    {
        PICO_LOG(LogEngine, Error, "PreInit called more than once");
        return 1;
    }

    bExited = false;
    FCommandLine::Init(Argc, Argv);
    FPaths::Init(Argc > 0 ? Argv[0] : "");
    FApp::Init(PICO_PROJECT_NAME);
    bPreInitialized = true;

    FConfigFile Config;
    const std::filesystem::path ConfigPath = FPaths::GetProjectConfigFile("Pico.ini");
    if (Config.Load(ConfigPath))
    {
        PICO_LOG(LogConfig, Info, "PreInit: loaded {}", ConfigPath.string());
    }
    else
    {
        PICO_LOG(LogConfig, Warning, "PreInit: {} was not found, using defaults", ConfigPath.string());
    }

    MaxFrameCount = Config.GetInt("Engine", "MaxFrameCount", -1);
    MaxFPS = Config.GetDouble("Engine", "MaxFPS", 60.0);

    if (const std::optional<int> CommandLineFrames = FCommandLine::GetInt("frames"))
    {
        MaxFrameCount = *CommandLineFrames;
    }

    if (const std::optional<int> CommandLineMaxFPS = FCommandLine::GetInt("maxfps"))
    {
        MaxFPS = *CommandLineMaxFPS;
    }

    if (MaxFrameCount < -1)
    {
        PICO_LOG(LogEngine, Error, "PreInit: max frames must be -1 or greater, got {}", MaxFrameCount);
        return 1;
    }

    if (!std::isfinite(MaxFPS) || MaxFPS < 0.0)
    {
        PICO_LOG(LogEngine, Error, "PreInit: max fps must be finite and non-negative, got {}", MaxFPS);
        return 1;
    }

    PICO_LOG(LogEngine, Info, "PreInit: project={} version={}", FApp::GetProjectName(), PICO_VERSION);
    PICO_LOG(LogPaths, Info, "PreInit: project root={}", FPaths::GetProjectRootDir().string());
    PICO_LOG(LogEngine, Info, "PreInit: max frames={}", MaxFrameCount);
    PICO_LOG(LogEngine, Info, "PreInit: max fps={}", MaxFPS);

    return 0;
}

int FEngineLoop::Init()
{
    if (!bPreInitialized || bInitialized)
    {
        PICO_LOG(LogEngine, Error, "Init called in an invalid engine loop state");
        return 1;
    }

    if (!PObjectSystem::Init())
    {
        PICO_LOG(LogEngine, Error, "Init: object system initialization failed");
        return 1;
    }

    bObjectSystemInitialized = true;

    if (!PActor::RegisterClass() || !PLevel::RegisterClass() || !PWorld::RegisterClass())
    {
        PICO_LOG(LogEngine, Error, "Init: engine class registration failed");
        return 1;
    }

    PWorld* World = NewObject<PWorld>(nullptr, "GameWorld");
    if (World == nullptr)
    {
        PICO_LOG(LogEngine, Error, "Init: world creation failed");
        return 1;
    }

    WorldHandle = World->GetHandle();
    if (!World->Initialize())
    {
        PICO_LOG(LogEngine, Error, "Init: world initialization failed");
        DestroyObjectTree(World);
        WorldHandle = {};
        return 1;
    }

    FrameTimer.Reset();
    bInitialized = true;

    if (MaxFrameCount == 0)
    {
        FApp::RequestExit();
    }

    PICO_LOG(LogEngine, Info, "Init: engine loop initialized");

    return 0;
}

void FEngineLoop::Tick()
{
    if (!bInitialized)
    {
        PICO_LOG(LogEngine, Error, "Tick called before Init");
        FApp::RequestExit();
        return;
    }

    FApp::BeginFrame();
    FrameTimer.Tick();

    PWorld* World = GetWorld();
    if (World == nullptr)
    {
        PICO_LOG(LogEngine, Error, "Tick: the active world is no longer available");
        FApp::RequestExit();
        return;
    }

    World->Tick(static_cast<float>(FrameTimer.GetDeltaSeconds()));

    PICO_LOG(
        LogEngine,
        Trace,
        "Tick: frame={} delta={:.6f}s total={:.6f}s avg={:.3f}ms fps={:.1f}",
        FApp::GetFrameCounter(),
        FrameTimer.GetDeltaSeconds(),
        FrameTimer.GetTotalSeconds(),
        FrameTimer.GetAverageFrameTimeMS(),
        FrameTimer.GetAverageFPS());

    if (MaxFrameCount >= 0 && FApp::GetFrameCounter() >= static_cast<uint64>(MaxFrameCount))
    {
        FApp::RequestExit();
    }

    if (!ShouldExit())
    {
        FrameTimer.WaitForMaxFPS(MaxFPS);
    }
}

void FEngineLoop::Exit()
{
    if (bExited)
    {
        return;
    }

    const bool bHadInitializedState = bPreInitialized || bObjectSystemInitialized || bInitialized;

    bInitialized = false;
    if (PWorld* World = GetWorld())
    {
        World->TearDown();
        DestroyObjectTree(World);
    }
    WorldHandle = {};

    if (bObjectSystemInitialized)
    {
        PObjectSystem::Shutdown();
        bObjectSystemInitialized = false;
    }

    if (bHadInitializedState)
    {
        PICO_LOG(LogEngine, Info, "Exit: final frame={}", FApp::GetFrameCounter());
    }

    bPreInitialized = false;
    bExited = true;
}

bool FEngineLoop::ShouldExit() const
{
    return FApp::IsExitRequested();
}

PWorld* FEngineLoop::GetWorld() const
{
    PObject* Object = ResolveObject(WorldHandle);
    return Object != nullptr && Object->IsA(PWorld::StaticClass())
        ? static_cast<PWorld*>(Object)
        : nullptr;
}

int GuardedMain(int Argc, char** Argv)
{
    FEngineLoop EngineLoop;
    int Result = 1;

    try
    {
        Result = EngineLoop.PreInit(Argc, Argv);
        if (Result == 0)
        {
            Result = EngineLoop.Init();
        }

        if (Result == 0)
        {
            while (!EngineLoop.ShouldExit())
            {
                EngineLoop.Tick();
            }
        }
    }
    catch (const std::exception& Exception)
    {
        PICO_LOG(LogEngine, Error, "Fatal error: {}", Exception.what());
        Result = 1;
    }
    catch (...)
    {
        PICO_LOG(LogEngine, Error, "Fatal error: unknown exception");
        Result = 1;
    }

    EngineLoop.Exit();
    return Result;
}
}
