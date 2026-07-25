#include "TestRunner.h"

#include "Pico/Core/App.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Object/ObjectSystem.h"

namespace
{
void TestTwoFrameLifecycle(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=2";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const int Result = Pico::GuardedMain(3, Arguments);

    Runner.Expect(Result == 0, "GuardedMain completes successfully");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 2, "Command line frame limit stops after two frames");
    Runner.Expect(Pico::FApp::IsExitRequested(), "Engine loop records the exit request");
    Runner.Expect(Pico::FApp::GetProjectName() == "Pico", "PreInit initializes the project name");
    Runner.Expect(!Pico::PObjectSystem::IsInitialized(), "GuardedMain shuts down the object system");
}

void TestZeroFrameLifecycle(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=0";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const int Result = Pico::GuardedMain(3, Arguments);

    Runner.Expect(Result == 0, "A zero-frame run completes successfully");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 0, "A zero-frame run does not tick");
    Runner.Expect(Pico::FApp::IsExitRequested(), "A zero-frame run requests exit during initialization");
}

void TestInvalidFrameLimit(FTestRunner& Runner)
{
    char Program[] = "PicoEngineTests";
    char Frames[] = "-frames=-2";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, Frames, MaxFPS };

    const int Result = Pico::GuardedMain(3, Arguments);

    Runner.Expect(Result != 0, "An invalid frame limit fails during PreInit");
    Runner.Expect(Pico::FApp::GetFrameCounter() == 0, "An invalid frame limit never ticks");
}
}

int main()
{
    FTestRunner Runner;
    TestTwoFrameLifecycle(Runner);
    TestZeroFrameLifecycle(Runner);
    TestInvalidFrameLimit(Runner);
    return Runner.Finish();
}
