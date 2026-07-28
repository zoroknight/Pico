#pragma once

#include "Pico/Core/Time.h"
#include "Pico/Object/ObjectTypes.h"

namespace Pico
{
class PWorld;

class FEngineLoop
{
public:
    int PreInit(int Argc, char** Argv);
    int Init();
    void Tick();
    void Exit();

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
    bool bExited = false;
};

int GuardedMain(int Argc, char** Argv);
}
