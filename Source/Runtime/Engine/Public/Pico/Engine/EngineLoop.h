#pragma once

#include "Pico/Core/Time.h"
#include "Pico/Object/ObjectTypes.h"

#include <filesystem>

namespace Pico
{
class PWorld;
enum class EWorldSerializationError;

class FEngineLoop
{
public:
    int PreInit(
        int Argc,
        char** Argv,
        const std::filesystem::path& ProjectFile = {});
    int Init();
    void Tick();
    void Exit();
    bool LoadWorld(
        const std::filesystem::path& FilePath,
        EWorldSerializationError* OutError = nullptr);

    bool ShouldExit() const;
    PWorld* GetWorld() const;

private:
    FFrameTimer FrameTimer;
    FObjectHandle WorldHandle;
    int MaxFrameCount = -1;
    double MaxFPS = 60.0;
    bool bPreInitialized = false;
    bool bObjectSystemInitialized = false;
    bool bInitialized = false;
    bool bTickingWorld = false;
    bool bExited = false;
};

int GuardedMain(int Argc, char** Argv);
}
