#include "Pico/Engine/EngineLoop.h"

#include "Pico/Core/ScopeExit.h"

#include "Pico/Core/App.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/ProjectDescriptor.h"
#include "Pico/Core/Types.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CapsuleComponent.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/DirectionalLightComponent.h"
#include "Pico/Engine/Controller.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/LightComponent.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/MovementComponent.h"
#include "Pico/Engine/Player.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PawnMovementComponent.h"
#include "Pico/Engine/FloatingPawnMovement.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/PointLightComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <string>

#ifndef PICO_PROJECT_NAME
#define PICO_PROJECT_NAME "Pico"
#endif

#ifndef PICO_VERSION
#define PICO_VERSION "0.1.0"
#endif

namespace Pico
{
int FEngineLoop::PreInit(
    int Argc,
    char** Argv,
    const std::filesystem::path& ProjectFile)
{
    if (bPreInitialized || bInitialized)
    {
        PICO_LOG(LogEngine, Error, "PreInit called more than once");
        return 1;
    }

    bExited = false;
    FCommandLine::Init(Argc, Argv);

    std::filesystem::path RequestedProjectFile = ProjectFile;
    if (RequestedProjectFile.empty())
    {
        if (const std::optional<std::string> ProjectArgument =
                FCommandLine::GetValue("project"))
        {
            RequestedProjectFile = *ProjectArgument;
        }
    }
    if (RequestedProjectFile.empty())
    {
        const std::vector<std::string>& Arguments = FCommandLine::GetArguments();
        for (std::size_t Index = 1; Index < Arguments.size(); ++Index)
        {
            const std::filesystem::path PositionalPath(Arguments[Index]);
            if (!Arguments[Index].starts_with("-")
                && PositionalPath.extension() == ".pico")
            {
                RequestedProjectFile = PositionalPath;
                break;
            }
        }
    }

    FPathInitOptions PathOptions;
    if (const std::optional<std::string> EngineRootArgument =
            FCommandLine::GetValue("engineroot"))
    {
        PathOptions.ExplicitEngineRoot = *EngineRootArgument;
    }
    if (const std::optional<std::string> StageRootArgument =
            FCommandLine::GetValue("stageroot"))
    {
        PathOptions.ExplicitStageRoot = *StageRootArgument;
    }
    if (!PathOptions.ExplicitEngineRoot.empty()
        && !PathOptions.ExplicitStageRoot.empty())
    {
        PICO_LOG(
            LogPaths,
            Error,
            "PreInit: -engineroot and -stageroot cannot be used together");
        return 1;
    }

    if (!FPaths::Init(
            Argc > 0 ? Argv[0] : "",
            RequestedProjectFile,
            PathOptions))
    {
        PICO_LOG(LogPaths, Error, "PreInit: engine or project paths could not be initialized");
        return 1;
    }

    std::string ProjectName = PICO_PROJECT_NAME;
    if (FPaths::HasProject())
    {
        FProjectDescriptor Descriptor;
        std::string DescriptorError;
        if (!FProjectDescriptor::Load(
                FPaths::GetProjectFile(),
                Descriptor,
                &DescriptorError))
        {
            PICO_LOG(
                LogPaths,
                Error,
                "PreInit: invalid project descriptor '{}': {}",
                FPaths::GetProjectFile().string(),
                DescriptorError);
            return 1;
        }
        ProjectName = Descriptor.GetName();
        if (FPaths::IsStaged()
            && ProjectName != FPaths::GetStageProjectName())
        {
            PICO_LOG(
                LogPaths,
                Error,
                "PreInit: Stage project '{}' does not match descriptor '{}'",
                FPaths::GetStageProjectName(),
                ProjectName);
            return 1;
        }
        if (!Descriptor.GetEngineVersion().empty()
            && Descriptor.GetEngineVersion() != PICO_VERSION)
        {
            PICO_LOG(
                LogPaths,
                Error,
                "PreInit: project requires Engine {}, runtime is {}",
                Descriptor.GetEngineVersion(),
                PICO_VERSION);
            return 1;
        }
    }

    if (!FPaths::GetLayoutEngineVersion().empty()
        && FPaths::GetLayoutEngineVersion() != PICO_VERSION)
    {
        PICO_LOG(
            LogPaths,
            Error,
            "PreInit: layout requires Engine {}, runtime is {}",
            FPaths::GetLayoutEngineVersion(),
            PICO_VERSION);
        return 1;
    }

    FApp::Init(ProjectName);
    bPreInitialized = true;

    FConfigFile Config;
    std::filesystem::path ConfigPath;
    if (FPaths::HasProject())
    {
        ConfigPath = FPaths::GetProjectConfigFile("Pico.ini");
    }
    if (ConfigPath.empty() || !std::filesystem::is_regular_file(ConfigPath))
    {
        ConfigPath = FPaths::GetEngineConfigFile("Pico.ini");
    }
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
    GarbageCollectionIntervalSeconds = Config.GetDouble(
        "Engine", "GarbageCollectionIntervalSeconds", 60.0);

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

    if (!std::isfinite(GarbageCollectionIntervalSeconds)
        || GarbageCollectionIntervalSeconds < 0.0)
    {
        PICO_LOG(
            LogEngine,
            Error,
            "PreInit: GC interval must be finite and non-negative, got {}",
            GarbageCollectionIntervalSeconds);
        return 1;
    }

    PICO_LOG(LogEngine, Info, "PreInit: project={} version={}", FApp::GetProjectName(), PICO_VERSION);
    PICO_LOG(LogPaths, Info, "PreInit: engine root={}", FPaths::GetEngineRootDir().string());
    PICO_LOG(
        LogPaths,
        Info,
        "PreInit: layout={}",
        FPaths::IsStaged() ? "Staged"
            : FPaths::GetEngineLayoutMode() == EEngineLayoutMode::Installed
                ? "Installed" : "Development");
    if (FPaths::IsStaged())
    {
        PICO_LOG(LogPaths, Info, "PreInit: stage root={}", FPaths::GetStageRootDir().string());
    }
    if (FPaths::HasProject())
    {
        PICO_LOG(LogPaths, Info, "PreInit: project file={}", FPaths::GetProjectFile().string());
        PICO_LOG(LogPaths, Info, "PreInit: project root={}", FPaths::GetProjectRootDir().string());
    }
    PICO_LOG(LogEngine, Info, "PreInit: max frames={}", MaxFrameCount);
    PICO_LOG(LogEngine, Info, "PreInit: max fps={}", MaxFPS);
    PICO_LOG(
        LogEngine,
        Info,
        "PreInit: GC interval={}s (0 disables timed requests)",
        GarbageCollectionIntervalSeconds);

    return 0;
}

int FEngineLoop::Init()
{
    if (!bPreInitialized || bInitialized)
    {
        PICO_LOG(LogEngine, Error, "Init called in an invalid engine loop state");
        return 1;
    }

    FAssetScanReport AssetScanReport;
    if (!AssetRegistry.ScanProjectContent(&AssetScanReport))
    {
        PICO_LOG(
            LogAsset,
            Warning,
            "Init: asset scan failed with {} issue(s)",
            AssetScanReport.Issues.size());
    }
    else if (FPaths::HasProject())
    {
        PICO_LOG(
            LogAsset,
            Info,
            "Init: registered {} asset(s), ignored {} file(s), issues={}",
            AssetScanReport.RegisteredAssetCount,
            AssetScanReport.IgnoredFileCount,
            AssetScanReport.Issues.size());
    }
    if (!PObjectSystem::Init())
    {
        PICO_LOG(LogEngine, Error, "Init: object system initialization failed");
        return 1;
    }

    bObjectSystemInitialized = true;

    if (!PPlayer::RegisterClass()
        || !PLocalPlayer::RegisterClass()
        || !PGameInstance::RegisterClass()
        || !PAnimInstance::RegisterClass()
        || !PActorComponent::RegisterClass()
        || !PSceneComponent::RegisterClass()
        || !PMovementComponent::RegisterClass()
        || !PPawnMovementComponent::RegisterClass()
        || !PFloatingPawnMovement::RegisterClass()
        || !PCharacterMovementComponent::RegisterClass()
        || !PCameraComponent::RegisterClass()
        || !PLightComponent::RegisterClass()
        || !PDirectionalLightComponent::RegisterClass()
        || !PPointLightComponent::RegisterClass()
        || !PSpringArmComponent::RegisterClass()
        || !PPrimitiveComponent::RegisterClass()
        || !PCubeComponent::RegisterClass()
        || !PCapsuleComponent::RegisterClass()
        || !PStaticMeshComponent::RegisterClass()
        || !PSkeletalMeshComponent::RegisterClass()
        || !PActor::RegisterClass()
        || !PPawn::RegisterClass()
        || !PCharacter::RegisterClass()
        || !PController::RegisterClass()
        || !PPlayerState::RegisterClass()
        || !PPlayerController::RegisterClass()
        || !PGameStateBase::RegisterClass()
        || !PGameModeBase::RegisterClass()
        || !PPlayerStart::RegisterClass()
        || !PLevel::RegisterClass()
        || !PWorld::RegisterClass())
    {
        PICO_LOG(LogEngine, Error, "Init: engine class registration failed");
        return 1;
    }

    PWorld* World = NewObject<PWorld>(nullptr, "GameWorld", EObjectFlags::RootSet);
    if (World == nullptr)
    {
        PICO_LOG(LogEngine, Error, "Init: world creation failed");
        return 1;
    }

    World->SetAssetServices(&AssetRegistry, &AssetManager);

    WorldHandle = World->GetHandle();
    if (!World->Initialize())
    {
        PICO_LOG(LogEngine, Error, "Init: world initialization failed");
        DestroyObjectTree(World);
        WorldHandle = {};
        return 1;
    }

    FrameTimer.Reset();
    GarbageCollectionElapsedSeconds = 0.0;
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

    bTickingWorld = true;
    auto ResetTickingWorld = MakeScopeExit(
        [this]()
        {
            bTickingWorld = false;
        });
    World->Tick(static_cast<float>(FrameTimer.GetDeltaSeconds()));
    ResetTickingWorld.Release();
    bTickingWorld = false;

    GarbageCollectionElapsedSeconds += FrameTimer.GetDeltaSeconds();
    if (GarbageCollectionIntervalSeconds > 0.0
        && GarbageCollectionElapsedSeconds >= GarbageCollectionIntervalSeconds)
    {
        RequestGarbageCollection(EGarbageCollectionReason::TimeLimit);
    }
    RunGarbageCollectionSafePoint();

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
    bTickingWorld = false;
    if (PWorld* World = GetWorld())
    {
        World->TearDown();
        DestroyObjectTree(World);
    }
    WorldHandle = {};
    if (bObjectSystemInitialized)
    {
        RequestGarbageCollection(EGarbageCollectionReason::EngineExit);
        RunGarbageCollectionSafePoint();
    }
    AssetManager.Clear();
    AssetRegistry.Clear();

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

bool FEngineLoop::LoadWorld(
    const std::filesystem::path& FilePath,
    EWorldSerializationError* OutError)
{
    FWorldAssetData Data;
    if (!LoadWorldAssetDataFromFile(FilePath, Data, OutError))
    {
        return false;
    }
    return ReplaceWorld(Data, OutError);
}

bool FEngineLoop::ReplaceWorld(
    const FWorldAssetData& Data,
    EWorldSerializationError* OutError,
    FWorldLoadOptions Options)
{
    if (OutError != nullptr)
    {
        *OutError = EWorldSerializationError::None;
    }
    PWorld* OldWorld = GetWorld();
    if (!bInitialized
        || bTickingWorld
        || OldWorld == nullptr
        || !ValidateWorldAssetData(Data, OutError))
    {
        if (OutError != nullptr && *OutError == EWorldSerializationError::None)
        {
            *OutError = EWorldSerializationError::InvalidArgument;
        }
        return false;
    }

    const auto WorldRecord = std::find_if(
        Data.Objects.begin(),
        Data.Objects.end(),
        [&Data](const FSceneObjectRecord& Record)
        {
            return Record.Id == Data.WorldId;
        });
    if (WorldRecord == Data.Objects.end())
    {
        if (OutError != nullptr)
        {
            *OutError = EWorldSerializationError::InvalidObjectGraph;
        }
        return false;
    }

    const FName TargetName(WorldRecord->ObjectName);
    PObject* ExistingTarget = FindObject(nullptr, TargetName);
    if (ExistingTarget != nullptr && ExistingTarget != OldWorld)
    {
        if (OutError != nullptr)
        {
            *OutError = EWorldSerializationError::WorldReplacementFailed;
        }
        return false;
    }

    const FName OldName = OldWorld->GetName();
    bool bRenamedOldWorld = false;
    if (ExistingTarget == OldWorld)
    {
        FName TemporaryName;
        for (uint64 Attempt = 1; Attempt != 0; ++Attempt)
        {
            TemporaryName = FName(
                "__PicoPreviousWorld_" + std::to_string(Attempt));
            if (FindObject(nullptr, TemporaryName) == nullptr)
            {
                break;
            }
        }

        if (TemporaryName.IsNone()
            || !RenameObject(OldWorld, TemporaryName))
        {
            if (OutError != nullptr)
            {
                *OutError = EWorldSerializationError::WorldReplacementFailed;
            }
            return false;
        }
        bRenamedOldWorld = true;
    }

    PWorld* NewWorld = CreateWorldFromAssetData(Data, OutError, Options);
    if (NewWorld == nullptr)
    {
        if (bRenamedOldWorld && !RenameObject(OldWorld, OldName))
        {
            if (OutError != nullptr)
            {
                *OutError = EWorldSerializationError::WorldReplacementFailed;
            }
        }
        return false;
    }

    NewWorld->SetAssetServices(&AssetRegistry, &AssetManager);

    if (!AddToRoot(NewWorld))
    {
        DestroyObjectTree(NewWorld);
        if (bRenamedOldWorld)
        {
            RenameObject(OldWorld, OldName);
        }
        if (OutError != nullptr)
        {
            *OutError = EWorldSerializationError::WorldReplacementFailed;
        }
        return false;
    }

    WorldHandle = NewWorld->GetHandle();
    OldWorld->TearDown();
    DestroyObjectTree(OldWorld);
    RequestGarbageCollection(EGarbageCollectionReason::WorldTransition);
    RunGarbageCollectionSafePoint();
    return true;
}

void FEngineLoop::RunGarbageCollectionSafePoint()
{
    if (!bObjectSystemInitialized || bTickingWorld || !IsGarbageCollectionRequested())
    {
        return;
    }

    const EGarbageCollectionReason Reasons = GetPendingGarbageCollectionReasons();
    FGarbageCollectionResult Result;
    if (!CollectGarbageIfRequested(&Result))
    {
        PICO_LOG(LogObject, Warning, "GC safe point rejected a pending collection");
        return;
    }

    GarbageCollectionElapsedSeconds = 0.0;
    PICO_LOG(
        LogObject,
        Info,
        "GC safe point: reasons={} before={} roots={} reachable={} collected={} after={}",
        static_cast<std::uint32_t>(Reasons),
        Result.ObjectCountBefore,
        Result.RootCount,
        Result.ReachableObjectCount,
        Result.CollectedObjectCount,
        Result.ObjectCountAfter);
}

bool FEngineLoop::ShouldExit() const
{
    return FApp::IsExitRequested();
}

float FEngineLoop::GetDeltaSeconds() const
{
    return static_cast<float>(FrameTimer.GetDeltaSeconds());
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

FAssetRegistry& FEngineLoop::GetAssetRegistry()
{
    return AssetRegistry;
}

const FAssetRegistry& FEngineLoop::GetAssetRegistry() const
{
    return AssetRegistry;
}

FAssetManager& FEngineLoop::GetAssetManager()
{
    return AssetManager;
}

const FAssetManager& FEngineLoop::GetAssetManager() const
{
    return AssetManager;
}
}
