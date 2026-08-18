#include "TestRunner.h"

#include "Pico/Core/App.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Delegate.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Math/Math.h"
#include "Pico/Core/Name.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/PlatformProcess.h"
#include "Pico/Core/PlatformTextInput.h"
#include "Pico/Core/ProjectDescriptor.h"
#include "Pico/Core/Time.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
int GStaticDelegateTotal = 0;

void AccumulateStaticDelegate(int Value)
{
    GStaticDelegateTotal += Value;
}

int DoubleDelegateValue(int Value)
{
    return Value * 2;
}

void TestDelegates(FTestRunner& Runner)
{
    Pico::TDelegate<int(int)> SingleDelegate;
    Runner.Expect(
        !SingleDelegate.IsBound()
            && !SingleDelegate.ExecuteIfBound(3).has_value(),
        "An empty single-cast delegate is safely unbound");
    SingleDelegate.BindStatic(&DoubleDelegateValue);
    Runner.Expect(
        SingleDelegate.IsBound()
            && SingleDelegate.Execute(4) == 8
            && SingleDelegate.ExecuteIfBound(5) == 10,
        "A single-cast delegate invokes a type-safe static function");
    SingleDelegate.BindLambda([](int Value) { return Value + 7; });
    Runner.Expect(
        SingleDelegate.Execute(3) == 10,
        "A single-cast delegate replaces its binding with a compatible lambda");
    SingleDelegate.Unbind();
    Runner.Expect(!SingleDelegate.IsBound(), "A single-cast delegate can be unbound");

    Pico::TDelegate<void(int)> VoidDelegate;
    int VoidDelegateTotal = 0;
    const bool bEmptyVoidSkipped = !VoidDelegate.ExecuteIfBound(2);
    VoidDelegate.BindLambda(
        [&VoidDelegateTotal](int Value)
        {
            VoidDelegateTotal += Value;
        });
    Runner.Expect(
        bEmptyVoidSkipped
            && VoidDelegate.ExecuteIfBound(3)
            && VoidDelegateTotal == 3,
        "A void single-cast delegate reports whether ExecuteIfBound invoked a callback");

    Pico::TMulticastDelegate<void(int)> StaticDelegate;
    GStaticDelegateTotal = 0;
    const Pico::FDelegateHandle StaticHandle =
        StaticDelegate.AddStatic(&AccumulateStaticDelegate);
    StaticDelegate.Broadcast(6);
    Runner.Expect(
        StaticHandle.IsValid()
            && GStaticDelegateTotal == 6
            && StaticDelegate.Remove(StaticHandle)
            && !StaticDelegate.Remove(StaticHandle),
        "A multicast delegate adds and removes a static listener by stable handle");

    Pico::TMulticastDelegate<void()> MutableDelegate;
    std::vector<int> Calls;
    bool bAddedDuringBroadcast = false;
    Pico::FDelegateHandle SelfHandle;
    Pico::FDelegateHandle RemovedBeforeTurnHandle;
    MutableDelegate.AddLambda(
        [&]()
        {
            Calls.push_back(1);
            MutableDelegate.Remove(RemovedBeforeTurnHandle);
            if (!bAddedDuringBroadcast)
            {
                bAddedDuringBroadcast = true;
                MutableDelegate.AddLambda([&Calls]() { Calls.push_back(4); });
            }
        });
    SelfHandle = MutableDelegate.AddLambda(
        [&]()
        {
            Calls.push_back(2);
            MutableDelegate.Remove(SelfHandle);
        });
    RemovedBeforeTurnHandle =
        MutableDelegate.AddLambda([&Calls]() { Calls.push_back(3); });
    MutableDelegate.Broadcast();
    Runner.Expect(
        Calls == std::vector<int>({1, 2}),
        "Broadcast supports self-removal and skips listeners removed before their turn");
    Calls.clear();
    MutableDelegate.Broadcast();
    Runner.Expect(
        Calls == std::vector<int>({1, 4}),
        "Listeners added during a broadcast begin on the next broadcast");

    Pico::TMulticastDelegate<void()> ClearingDelegate;
    Calls.clear();
    ClearingDelegate.AddLambda(
        [&]()
        {
            Calls.push_back(1);
            ClearingDelegate.Clear();
        });
    ClearingDelegate.AddLambda([&Calls]() { Calls.push_back(2); });
    ClearingDelegate.Broadcast();
    Runner.Expect(
        Calls == std::vector<int>({1}) && !ClearingDelegate.IsBound(),
        "Clear during broadcast prevents remaining callbacks and compacts bindings");

    Pico::TMulticastDelegate<void(int)> NestedDelegate;
    Calls.clear();
    bool bInsideNestedBroadcast = false;
    NestedDelegate.AddLambda(
        [&](int Value)
        {
            Calls.push_back(Value);
            if (!bInsideNestedBroadcast)
            {
                bInsideNestedBroadcast = true;
                NestedDelegate.Broadcast(2);
                bInsideNestedBroadcast = false;
            }
        });
    NestedDelegate.AddLambda([&Calls](int Value) { Calls.push_back(Value * 10); });
    NestedDelegate.Broadcast(1);
    Runner.Expect(
        Calls == std::vector<int>({1, 2, 20, 10}),
        "Nested broadcasts use independent stable listener snapshots");

    Pico::TMulticastDelegate<void()> ThrowingDelegate;
    Pico::FDelegateHandle ThrowingHandle;
    int SurvivorCalls = 0;
    ThrowingHandle = ThrowingDelegate.AddLambda(
        [&]()
        {
            ThrowingDelegate.Remove(ThrowingHandle);
            throw std::runtime_error("delegate test");
        });
    ThrowingDelegate.AddLambda([&SurvivorCalls]() { ++SurvivorCalls; });
    bool bExceptionObserved = false;
    try
    {
        ThrowingDelegate.Broadcast();
    }
    catch (const std::runtime_error&)
    {
        bExceptionObserved = true;
    }
    ThrowingDelegate.Broadcast();
    Runner.Expect(
        bExceptionObserved
            && SurvivorCalls == 1
            && ThrowingDelegate.Num() == 1,
        "An exception restores broadcast state and preserves surviving listeners");
}

void TestPlatformTextInput(FTestRunner& Runner)
{
    Pico::FPlatformTextInputContext TextInput;
    Runner.Expect(
        TextInput.IsTextInputEnabled(),
        "Platform text input starts enabled");

    TextInput.SetTextInputEnabled(false);
    TextInput.SetTextInputEnabled(false);
    Runner.Expect(
        !TextInput.IsTextInputEnabled(),
        "Platform text input disable is idempotent");

    Pico::FPlatformTextInputContext MovedTextInput(std::move(TextInput));
    Runner.Expect(
        !MovedTextInput.IsTextInputEnabled()
            && TextInput.IsTextInputEnabled(),
        "Platform text input moves disabled-window ownership safely");

    MovedTextInput.SetTextInputEnabled(true);
    Runner.Expect(
        MovedTextInput.IsTextInputEnabled(),
        "Platform text input restores the previous input context");
}

void TestAssetPath(FTestRunner& Runner)
{
    Pico::FAssetPath Path;
    Pico::EAssetPathError Error = Pico::EAssetPathError::None;
    Runner.Expect(
        Pico::FAssetPath::TryParse("/Game/Meshes/Robot.pmesh", Path, &Error)
            && Error == Pico::EAssetPathError::None
            && Path.ToString() == "/Game/Meshes/Robot.pmesh"
            && Path.GetGameRelativePath() == "Meshes/Robot.pmesh"
            && Path.GetExtension() == ".pmesh",
        "Asset paths expose a canonical /Game identity");

    Pico::FAssetPath WindowsStyle;
    Runner.Expect(
        Pico::FAssetPath::TryParse(
            "\\Game\\Textures\\Grid.ptex",
            WindowsStyle)
            && WindowsStyle.ToString() == "/Game/Textures/Grid.ptex",
        "Asset paths normalize directory separators");
    Runner.Expect(
        !Pico::FAssetPath::TryParse("C:/Game/Robot.pmesh", Path, &Error)
            && Error == Pico::EAssetPathError::InvalidRoot,
        "Asset paths reject disk paths");
    Runner.Expect(
        !Pico::FAssetPath::TryParse("/Game/../Source/Hacked.pmesh", Path, &Error)
            && Error == Pico::EAssetPathError::InvalidSegment,
        "Asset paths reject parent traversal");
    Runner.Expect(
        !Pico::FAssetPath::TryParse("/Game/Meshes/Robot", Path, &Error)
            && Error == Pico::EAssetPathError::MissingExtension,
        "Asset paths require an asset extension");
    Runner.Expect(
        !Path.IsValid(),
        "A failed asset path parse clears the output value");
}

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
    const std::filesystem::path FixturePath = Pico::FPaths::GetEngineRootDir()
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
    const std::filesystem::path EngineRoot = Pico::FPaths::GetEngineRootDir();
    const std::filesystem::path ExecutablePath = Pico::FPaths::GetExecutablePath();

    Runner.Expect(!Pico::FPaths::GetExecutablePath().empty(), "Paths records the executable path");
    Runner.Expect(!EngineRoot.empty(), "Paths finds the engine root");
    Runner.Expect(
        std::filesystem::exists(EngineRoot / "CMakeLists.txt"),
        "Engine root contains CMakeLists.txt");
    Runner.Expect(
        std::filesystem::exists(Pico::FPaths::GetEngineConfigFile("Pico.ini")),
        "Paths resolves the engine config file");
    Runner.Expect(
        !Pico::FPaths::HasProject() && Pico::FPaths::GetProjectRootDir().empty(),
        "Engine tools do not invent a project root");

    const auto UniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path TestRoot = std::filesystem::temp_directory_path()
        / ("PicoCoreTests_ProjectBoundary_" + std::to_string(UniqueSuffix));
    const std::filesystem::path ForeignDirectory = TestRoot / "Foreign";
    const std::filesystem::path ProjectRoot = TestRoot / "SampleProject";
    const std::filesystem::path ProjectFile = ProjectRoot / "SampleProject.pico";
    const std::filesystem::path OriginalWorkingDirectory = std::filesystem::current_path();
    std::error_code ErrorCode;

    std::filesystem::create_directories(ForeignDirectory, ErrorCode);
    std::ofstream(ForeignDirectory / "CMakeLists.txt") << "project(ForeignProject)\n";
    std::filesystem::current_path(ForeignDirectory, ErrorCode);
    const bool bChangedWorkingDirectory = !ErrorCode;

    if (bChangedWorkingDirectory)
    {
        Runner.Expect(
            Pico::FPaths::Init(ExecutablePath.string()),
            "Paths reinitialize from a foreign working directory");
    }

    std::filesystem::current_path(OriginalWorkingDirectory, ErrorCode);
    Runner.Expect(bChangedWorkingDirectory, "Path test enters a foreign CMake project");
    Runner.Expect(
        Pico::FPaths::GetEngineRootDir() == EngineRoot,
        "Paths ignores an unrelated CMake project in the working directory");

    std::filesystem::create_directories(ProjectRoot / "Config", ErrorCode);
    std::filesystem::create_directories(ProjectRoot / "Content", ErrorCode);
    std::filesystem::create_directories(ProjectRoot / "Intermediate", ErrorCode);
    std::filesystem::create_directories(ProjectRoot / "Saved", ErrorCode);
    std::filesystem::create_directories(ProjectRoot / "Source", ErrorCode);
    std::ofstream(ProjectFile)
        << "[Project]\n"
        << "Name=SampleProject\n"
        << "FileVersion=1\n"
        << "EngineVersion=0.1.0\n";

    Pico::FProjectDescriptor Descriptor;
    std::string DescriptorError;
    Runner.Expect(
        Pico::FProjectDescriptor::Load(ProjectFile, Descriptor, &DescriptorError),
        "Project descriptor loads a valid .pico file");
    Runner.Expect(
        Descriptor.GetName() == "SampleProject"
            && Descriptor.GetFileVersion() == 1
            && Descriptor.GetRootDir() == std::filesystem::weakly_canonical(ProjectRoot),
        "Project descriptor exposes stable project identity");

    Runner.Expect(
        Pico::FPaths::Init(ExecutablePath.string(), ProjectFile),
        "Paths initialize an explicit external project");
    Runner.Expect(
        Pico::FPaths::HasProject()
            && Pico::FPaths::GetProjectRootDir() == std::filesystem::weakly_canonical(ProjectRoot),
        "Engine and project roots are independent");

    std::filesystem::path WritePath;
    Runner.Expect(
        Pico::FPaths::TryGetProjectWritePath(
            Pico::EProjectWriteRoot::Content,
            std::filesystem::path("Maps") / "Main.pworld",
            WritePath)
            && WritePath == std::filesystem::weakly_canonical(
                ProjectRoot / "Content" / "Maps" / "Main.pworld"),
        "Content writes resolve inside the project");
    Runner.Expect(
        Pico::FPaths::IsProjectWritePath(WritePath),
        "Resolved Content path is an approved project write path");
    Runner.Expect(
        !Pico::FPaths::TryGetProjectWritePath(
            Pico::EProjectWriteRoot::Content,
            std::filesystem::path("..") / "Source" / "Hacked.cpp",
            WritePath),
        "Relative traversal cannot escape a project write root");
    Runner.Expect(
        !Pico::FPaths::IsProjectWritePath(ProjectRoot / "Source" / "Generated.cpp"),
        "Editor-generated files cannot target project Source");
    Runner.Expect(
        !Pico::FPaths::IsProjectWritePath(
            EngineRoot / "Source" / "Runtime" / "Core" / "Private" / "Hacked.cpp"),
        "Editor-generated files cannot target Engine Source");

    const std::filesystem::path InstalledRoot = TestRoot / "InstalledEngine";
    std::filesystem::create_directories(InstalledRoot / "Config", ErrorCode);
    std::ofstream(InstalledRoot / "Config" / "Pico.ini")
        << "[Engine]\nMaxFrameCount=-1\n";
    std::ofstream(InstalledRoot / "PicoEngine.root")
        << "[Engine]\n"
        << "Name=Pico\n"
        << "Version=0.1.0\n"
        << "LayoutVersion=1\n";

    Pico::FPathInitOptions InstalledOptions;
    InstalledOptions.ExplicitEngineRoot = InstalledRoot;
    Runner.Expect(
        Pico::FPaths::Init(
            ExecutablePath.string(), ProjectFile, InstalledOptions)
            && Pico::FPaths::GetEngineLayoutMode()
                == Pico::EEngineLayoutMode::Installed
            && Pico::FPaths::GetEngineRootDir()
                == std::filesystem::weakly_canonical(InstalledRoot)
            && Pico::FPaths::GetLayoutEngineVersion() == "0.1.0"
            && !Pico::FPaths::IsStaged(),
        "An explicit installed Engine root needs only its marker and Config");

    Pico::FPathInitOptions InvalidInstalledOptions;
    InvalidInstalledOptions.ExplicitEngineRoot = TestRoot / "MissingEngine";
    Runner.Expect(
        !Pico::FPaths::Init(
            ExecutablePath.string(), ProjectFile, InvalidInstalledOptions)
            && Pico::FPaths::GetEngineLayoutMode()
                == Pico::EEngineLayoutMode::Unknown,
        "An invalid explicit Engine root fails instead of falling back to the development tree");

    const std::filesystem::path StageRoot = TestRoot / "Stage";
    const std::filesystem::path StageEngineRoot = StageRoot / "Engine";
    const std::filesystem::path StageProjectRoot = StageRoot / "SampleProject";
    const std::filesystem::path StageProjectFile =
        StageProjectRoot / "SampleProject.pico";
    const std::filesystem::path StageExecutable =
        StageRoot / "Binaries" / "PicoSample.exe";
    std::filesystem::create_directories(StageEngineRoot / "Config", ErrorCode);
    std::filesystem::create_directories(StageProjectRoot / "Config", ErrorCode);
    std::filesystem::create_directories(StageProjectRoot / "Content", ErrorCode);
    std::filesystem::create_directories(StageExecutable.parent_path(), ErrorCode);
    std::ofstream(StageEngineRoot / "Config" / "Pico.ini")
        << "[Engine]\nMaxFrameCount=-1\n";
    std::ofstream(StageEngineRoot / "PicoEngine.root")
        << "[Engine]\nName=Pico\nVersion=0.1.0\nLayoutVersion=1\n";
    std::ofstream(StageProjectFile)
        << "[Project]\nName=SampleProject\nFileVersion=1\nEngineVersion=0.1.0\n";
    std::ofstream(StageRoot / "PicoStage.manifest")
        << "[Stage]\n"
        << "LayoutVersion=1\n"
        << "EngineVersion=0.1.0\n"
        << "ProjectName=SampleProject\n"
        << "EngineRelativePath=Engine\n"
        << "ProjectRelativePath=SampleProject/SampleProject.pico\n";

    Pico::FPathInitOptions StageOptions;
    StageOptions.ExplicitStageRoot = StageRoot;
    Runner.Expect(
        Pico::FPaths::Init(StageExecutable.string(), {}, StageOptions)
            && Pico::FPaths::IsStaged()
            && Pico::FPaths::GetStageRootDir()
                == std::filesystem::weakly_canonical(StageRoot)
            && Pico::FPaths::GetEngineRootDir()
                == std::filesystem::weakly_canonical(StageEngineRoot)
            && Pico::FPaths::GetProjectFile()
                == std::filesystem::weakly_canonical(StageProjectFile)
            && Pico::FPaths::GetLayoutEngineVersion() == "0.1.0"
            && Pico::FPaths::GetStageProjectName() == "SampleProject",
        "A Stage manifest resolves Engine and default Project paths without Source files");
    Runner.Expect(
        Pico::FPaths::Init(StageExecutable.string())
            && Pico::FPaths::IsStaged()
            && Pico::FPaths::GetProjectRootDir()
                == std::filesystem::weakly_canonical(StageProjectRoot),
        "An executable inside Stage discovers its manifest without a working-directory dependency");

    std::ofstream(StageEngineRoot / "PicoEngine.root", std::ios::trunc)
        << "[Engine]\nName=Pico\nVersion=9.9.9\nLayoutVersion=1\n";
    Runner.Expect(
        !Pico::FPaths::Init(StageExecutable.string(), {}, StageOptions),
        "Stage rejects mismatched Manifest and installed Engine versions");
    std::ofstream(StageEngineRoot / "PicoEngine.root", std::ios::trunc)
        << "[Engine]\nName=Pico\nVersion=0.1.0\nLayoutVersion=1\n";

    Pico::FPathInitOptions EscapingStageOptions;
    EscapingStageOptions.ExplicitStageRoot = StageRoot;
    Runner.Expect(
        !Pico::FPaths::Init(
            StageExecutable.string(), ProjectFile, EscapingStageOptions),
        "Staged runtime rejects a Project descriptor outside its Stage root");

    Runner.Expect(
        Pico::FPaths::Init(ExecutablePath.string()),
        "Paths restore development engine-only mode after layout tests");
    Runner.Expect(
        Pico::FPaths::GetEngineLayoutMode()
            == Pico::EEngineLayoutMode::Development
            && Pico::FPaths::GetEngineRootDir() == EngineRoot,
        "Development layout remains the default inside the source repository");
    std::filesystem::remove_all(TestRoot, ErrorCode);
}

void TestPlatformProcess(FTestRunner& Runner)
{
    std::string Error;
    Pico::FProcessHandle Child = Pico::FPlatformProcess::CreateProcess(
        Pico::FPaths::GetExecutablePath(),
        { "-platform-process-child", "argument with spaces" },
        Pico::FPaths::GetEngineRootDir(),
        &Error);
    Runner.Expect(
        Child.IsValid() && Child.GetProcessId() != 0,
        "Platform process creates a managed child process");

    int ExitCode = -1;
    Runner.Expect(
        Pico::FPlatformProcess::WaitForExit(Child, 5000, &ExitCode)
            && ExitCode == 0,
        "Platform process preserves quoted arguments and captures exit code");
    Runner.Expect(
        !Pico::FPlatformProcess::IsRunning(Child),
        "Platform process detects child exit");
    Child.Reset();
    Runner.Expect(!Child.IsValid(), "Process handle can be released explicitly");

    Pico::FProcessHandle Missing = Pico::FPlatformProcess::CreateProcess(
        Pico::FPaths::GetEngineRootDir() / "MissingPicoProgram.exe",
        {},
        {},
        &Error);
    Runner.Expect(
        !Missing.IsValid() && !Error.empty(),
        "Platform process reports a missing executable safely");
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

void TestVectorMath(FTestRunner& Runner)
{
    const Pico::FVector3 Vector(3.0f, 4.0f, 0.0f);
    Runner.Expect(Pico::IsNearlyEqual(Vector.Size(), 5.0f), "Vector reports its Euclidean length");
    Runner.Expect(
        Vector.GetSafeNormal().Equals(Pico::FVector3(0.6f, 0.8f, 0.0f)),
        "Vector produces a safe normalized copy");
    Runner.Expect(
        Pico::IsNearlyEqual(
            Pico::FVector3::Dot(Pico::FVector3::ForwardVector, Pico::FVector3::RightVector),
            0.0f),
        "Perpendicular vectors have a zero dot product");
    Runner.Expect(
        Pico::FVector3::Cross(Pico::FVector3::ForwardVector, Pico::FVector3::RightVector)
            .Equals(Pico::FVector3::UpVector),
        "Forward cross right produces the up axis");
    Runner.Expect(
        Pico::FVector3::ZeroVector.GetSafeNormal().Equals(Pico::FVector3::ZeroVector),
        "Normalizing a zero vector stays finite");
}

void TestRotationMath(FTestRunner& Runner)
{
    const Pico::FQuat Yaw90 = Pico::FRotator(0.0f, 90.0f, 0.0f).Quaternion();
    Runner.Expect(
        Yaw90.RotateVector(Pico::FVector3::ForwardVector).Equals(Pico::FVector3::RightVector),
        "Positive yaw rotates forward toward right");

    const Pico::FQuat PitchUp = Pico::FRotator(-90.0f, 0.0f, 0.0f).Quaternion();
    Runner.Expect(
        PitchUp.RotateVector(Pico::FVector3::ForwardVector).Equals(
            Pico::FVector3::UpVector),
        "Negative pitch rotates forward toward up");

    const Pico::FRotator SourceRotation(10.0f, 45.0f, 20.0f);
    const Pico::FRotator RoundTripRotation = SourceRotation.Quaternion().Rotator();
    Runner.Expect(
        RoundTripRotation.Equals(SourceRotation, 0.001f),
        "Rotator converts to a quaternion and back");

    Pico::FQuat InvalidRotation(0.0f, 0.0f, 0.0f, 0.0f);
    Runner.Expect(
        !InvalidRotation.Normalize() && InvalidRotation.Equals(Pico::FQuat::Identity),
        "Normalizing a zero quaternion falls back to identity");
}

void TestTransformMath(FTestRunner& Runner)
{
    const Pico::FTransform Transform(
        Pico::FRotator(0.0f, 90.0f, 0.0f),
        Pico::FVector3(100.0f, 20.0f, 5.0f),
        Pico::FVector3(2.0f, 2.0f, 2.0f));
    const Pico::FVector3 LocalPoint(10.0f, 0.0f, 0.0f);
    const Pico::FVector3 WorldPoint = Transform.TransformPosition(LocalPoint);
    Runner.Expect(
        WorldPoint.Equals(Pico::FVector3(100.0f, 40.0f, 5.0f)),
        "Transform applies scale, rotation, and translation in order");
    Runner.Expect(
        Transform.InverseTransformPosition(WorldPoint).Equals(LocalPoint),
        "InverseTransformPosition restores a local point");
    Runner.Expect(
        Transform.ToMatrix().TransformPosition(LocalPoint).Equals(WorldPoint),
        "Transform and matrix position results agree");

    const Pico::FTransform Parent(
        Pico::FRotator(0.0f, 90.0f, 0.0f),
        Pico::FVector3(100.0f, 0.0f, 0.0f),
        Pico::FVector3::OneVector);
    const Pico::FTransform Local(
        Pico::FRotator::ZeroRotator,
        Pico::FVector3(20.0f, 0.0f, 0.0f),
        Pico::FVector3::OneVector);
    const Pico::FTransform World = Local * Parent;
    Runner.Expect(
        World.Translation.Equals(Pico::FVector3(100.0f, 20.0f, 0.0f)),
        "Local times parent follows UE-style transform composition");
    Runner.Expect(
        World.GetRelativeTransform(Parent).Equals(Local),
        "Relative transform recovers local transform from world and parent");

    const Pico::FMatrix4 ComposedMatrix = Parent.ToMatrix() * Local.ToMatrix();
    Runner.Expect(
        World.ToMatrix().Equals(ComposedMatrix),
        "Transform composition matches matrix composition without shear");

    const Pico::FTransform ZeroScale(
        Pico::FQuat::Identity,
        Pico::FVector3::ZeroVector,
        Pico::FVector3(0.0f, 1.0f, 1.0f));
    Runner.Expect(
        ZeroScale.InverseTransformVector(Pico::FVector3::OneVector)
            .Equals(Pico::FVector3(0.0f, 1.0f, 1.0f)),
        "Inverse transform protects zero scale components");
}

void TestLogOutputs(FTestRunner& Runner)
{
    const std::filesystem::path LogFile =
        std::filesystem::temp_directory_path() / "PicoCoreLogTest.log";
    std::error_code Error;
    std::filesystem::remove(LogFile, Error);

    const std::uint64_t PreviousSequence = Pico::FLog::GetLatestSequence();
    Pico::FLog::SetConsoleOutputEnabled(false);
    Runner.Expect(
        !Pico::FLog::IsConsoleOutputEnabled(),
        "Log console output can be disabled independently");
    const bool bOpened = Pico::FLog::SetOutputFile(LogFile);
    Pico::FLog::Write(
        "LogTest", Pico::ELogLevel::Warning, "persistent warning marker");
    Pico::FLog::CloseOutputFile();

    std::ifstream Input(LogFile);
    const std::string Contents {
        std::istreambuf_iterator<char>(Input),
        std::istreambuf_iterator<char>()};
    Runner.Expect(
        bOpened && Contents.find("[LogTest][Warning] persistent warning marker")
            != std::string::npos,
        "Log records persist to a file while console output is disabled");

    const std::vector<Pico::FLogRecord> Records =
        Pico::FLog::GetRecordsSince(PreviousSequence);
    Runner.Expect(
        !Records.empty()
            && Records.back().Category == "LogTest"
            && Records.back().Level == Pico::ELogLevel::Warning,
        "Recent log history exposes structured records to editor tools");

    Pico::FLog::SetConsoleOutputEnabled(true);
    std::filesystem::remove(LogFile, Error);
}
}

int main(int Argc, char** Argv)
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (Argc == 3
        && std::string_view(Argv[1]) == "-platform-process-child")
    {
        return std::string_view(Argv[2]) == "argument with spaces" ? 0 : 7;
    }

    Pico::FPaths::Init(Argc > 0 ? Argv[0] : "PicoCoreTests");

    FTestRunner Runner;
    TestDelegates(Runner);
    TestPlatformTextInput(Runner);
    TestAssetPath(Runner);
    TestCommandLine(Runner);
    TestConfig(Runner);
    TestPaths(Runner);
    TestPlatformProcess(Runner);
    TestAppOwnsProjectName(Runner);
    TestName(Runner);
    TestFrameTimer(Runner);
    TestVectorMath(Runner);
    TestRotationMath(Runner);
    TestTransformMath(Runner);
    TestLogOutputs(Runner);
    return Runner.Finish();
}
