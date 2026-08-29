#pragma once

#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Core/Time.h"
#include "Pico/Engine/WorldSerialization.h"
#include "Pico/Object/ObjectTypes.h"

#include <filesystem>
#include <functional>

namespace Pico
{
class PWorld;

struct FEngineFrameCallbacks
{
    std::function<void(float)> BeforeWorldTick;
    std::function<void(float)> AfterWorldTick;
};

class FEngineLoop
{
public:
    int PreInit(
        int Argc,
        char** Argv,
        const std::filesystem::path& ProjectFile = {});
    int Init();
    void Tick();
    void Tick(const FEngineFrameCallbacks& Callbacks);
    void Exit();
    bool LoadWorld(
        const std::filesystem::path& FilePath,
        EWorldSerializationError* OutError = nullptr);
    bool ReplaceWorld(
        const FWorldAssetData& Data,
        EWorldSerializationError* OutError = nullptr,
        FWorldLoadOptions Options = {});

    bool ShouldExit() const;
    float GetDeltaSeconds() const;
    double GetAverageFrameTimeMS() const;
    double GetAverageFPS() const;
    const FFramePacingSettings& GetFramePacingSettings() const;
    EFramePacingMode GetFramePacingMode(bool bPresentationCanVSync) const;
    void WaitForFrameLimit(bool bPresentationCanVSync);
    PWorld* GetWorld() const;
    FAssetRegistry& GetAssetRegistry();
    const FAssetRegistry& GetAssetRegistry() const;
    FAssetManager& GetAssetManager();
    const FAssetManager& GetAssetManager() const;

private:
    void RunGarbageCollectionSafePoint();

    FAssetRegistry AssetRegistry;
    FAssetManager AssetManager;
    FFrameTimer FrameTimer;
    std::filesystem::path ProfileTracePath;
    FObjectHandle WorldHandle;
    int MaxFrameCount = -1;
    FFramePacingSettings FramePacingSettings;
    double GarbageCollectionIntervalSeconds = 60.0;
    double GarbageCollectionElapsedSeconds = 0.0;
    bool bPreInitialized = false;
    bool bObjectSystemInitialized = false;
    bool bInitialized = false;
    bool bTickingWorld = false;
    bool bExited = false;
};

int GuardedMain(int Argc, char** Argv);
}
