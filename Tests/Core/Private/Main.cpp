#include "TestRunner.h"

#include "Pico/Core/App.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Name.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/Time.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace
{
void TestCommandLine(FTestRunner& Runner)
{
    char Program[] = "PicoCoreTests";
    char Frames[] = "-frames=5";
    char Verbose[] = "-verbose";
    char InvalidNumber[] = "-invalid=abc";
    char* Arguments[] = { Program, Frames, Verbose, InvalidNumber };

    Pico::FCommandLine::Init(4, Arguments);

    Runner.Expect(Pico::FCommandLine::HasSwitch("verbose"), "Command line finds a switch");
    Runner.Expect(!Pico::FCommandLine::HasSwitch("missing"), "Command line rejects a missing switch");
    Runner.Expect(Pico::FCommandLine::GetInt("frames") == 5, "Command line parses an integer value");
    Runner.Expect(!Pico::FCommandLine::GetInt("invalid").has_value(), "Command line rejects an invalid integer");
    Runner.Expect(!Pico::FCommandLine::GetValue("missing").has_value(), "Command line reports a missing value");
}

void TestConfig(FTestRunner& Runner)
{
    const std::filesystem::path FixturePath = Pico::FPaths::GetProjectRootDir()
        / "Tests" / "Core" / "Fixtures" / "CoreTest.ini";

    Pico::FConfigFile Config;
    Runner.Expect(Config.Load(FixturePath), "Config loads the test fixture");
    Runner.Expect(Config.GetString("CoreTest", "Name", "") == "Pico Test", "Config reads a string");
    Runner.Expect(Config.GetInt("CoreTest", "MaxFrames", -1) == 5, "Config reads an integer");
    Runner.Expect(std::abs(Config.GetDouble("CoreTest", "FrameScale", 0.0) - 1.25) < 0.000001,
        "Config reads a floating-point value");
    Runner.Expect(Config.GetBool("CoreTest", "Enabled", false), "Config reads a boolean");
    Runner.Expect(Config.GetInt("CoreTest", "InvalidInt", 7) == 7, "Config defaults an invalid integer");
    Runner.Expect(Config.GetDouble("CoreTest", "InvalidDouble", 2.0) == 2.0,
        "Config defaults an invalid floating-point value");
    Runner.Expect(Config.GetDouble("CoreTest", "TrailingDouble", 3.0) == 3.0,
        "Config rejects trailing characters in a floating-point value");
    Runner.Expect(Config.GetBool("CoreTest", "InvalidBool", true), "Config defaults an invalid boolean");
    Runner.Expect(Config.GetInt("CoreTest", "Missing", 9) == 9, "Config defaults a missing key");
}

void TestPaths(FTestRunner& Runner)
{
    const std::filesystem::path& ProjectRoot = Pico::FPaths::GetProjectRootDir();
    const std::filesystem::path ExpectedProjectRoot = ProjectRoot;
    const std::filesystem::path ExecutablePath = Pico::FPaths::GetExecutablePath();

    Runner.Expect(!Pico::FPaths::GetExecutablePath().empty(), "Paths records the executable path");
    Runner.Expect(!ProjectRoot.empty(), "Paths finds a project root");
    Runner.Expect(std::filesystem::exists(ProjectRoot / "CMakeLists.txt"), "Project root contains CMakeLists.txt");
    Runner.Expect(std::filesystem::exists(Pico::FPaths::GetProjectConfigFile("Pico.ini")),
        "Paths resolves the project config file");

    const auto UniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path ForeignProject = std::filesystem::temp_directory_path()
        / ("PicoCoreTests_ForeignProject_" + std::to_string(UniqueSuffix));
    const std::filesystem::path OriginalWorkingDirectory = std::filesystem::current_path();
    std::error_code ErrorCode;

    std::filesystem::create_directories(ForeignProject, ErrorCode);
    std::ofstream(ForeignProject / "CMakeLists.txt") << "project(ForeignProject)\n";
    std::filesystem::current_path(ForeignProject, ErrorCode);
    const bool bChangedWorkingDirectory = !ErrorCode;

    if (bChangedWorkingDirectory)
    {
        Pico::FPaths::Init(ExecutablePath.string());
    }

    std::filesystem::current_path(OriginalWorkingDirectory, ErrorCode);
    Runner.Expect(bChangedWorkingDirectory, "Path test enters a foreign CMake project");
    Runner.Expect(Pico::FPaths::GetProjectRootDir() == ExpectedProjectRoot,
        "Paths ignores an unrelated CMake project in the working directory");

    std::filesystem::remove_all(ForeignProject, ErrorCode);
}

void TestAppOwnsProjectName(FTestRunner& Runner)
{
    std::string ProjectName = "OwnedProjectName";
    Pico::FApp::Init(ProjectName);
    ProjectName.assign("ChangedByCaller");

    Runner.Expect(Pico::FApp::GetProjectName() == "OwnedProjectName", "App owns the project name storage");
}

void TestName(FTestRunner& Runner)
{
    const Pico::FName None;
    const Pico::FName Empty("");
    const Pico::FName ExplicitNone("None");
    const Pico::FName PlayerA("Player");
    const Pico::FName PlayerB("Player");
    const Pico::FName Enemy("Enemy");

    Runner.Expect(None.IsNone(), "Default name is None");
    Runner.Expect(Empty == None && ExplicitNone == None, "Empty and explicit None names use the None entry");
    Runner.Expect(PlayerA == PlayerB, "Equal name text shares one comparison index");
    Runner.Expect(PlayerA != Enemy, "Different name text has a different identity");
    Runner.Expect(PlayerA.ToString() == "Player", "Name resolves back to its source text");

    std::unordered_map<Pico::FName, int, Pico::FNameHash> Values;
    Values.emplace(PlayerA, 7);
    Runner.Expect(Values.at(PlayerB) == 7, "Name can be used as a hash key");
}

void TestFrameTimer(FTestRunner& Runner)
{
    Pico::FFrameTimer Timer;
    Timer.Reset();
    Timer.Tick();

    Runner.Expect(Timer.GetDeltaSeconds() >= 0.0, "Frame timer produces a non-negative delta");
    Runner.Expect(Timer.GetTotalSeconds() >= 0.0, "Frame timer produces a non-negative total time");
    Runner.Expect(Timer.GetAverageFrameTimeMS() >= 0.0, "Frame timer produces a non-negative average frame time");
    Runner.Expect(Timer.GetAverageFPS() >= 0.0, "Frame timer produces a non-negative average FPS");
}
}

int main(int Argc, char** Argv)
{
    Pico::FPaths::Init(Argc > 0 ? Argv[0] : "PicoCoreTests");

    FTestRunner Runner;
    TestCommandLine(Runner);
    TestConfig(Runner);
    TestPaths(Runner);
    TestAppOwnsProjectName(Runner);
    TestName(Runner);
    TestFrameTimer(Runner);
    return Runner.Finish();
}
