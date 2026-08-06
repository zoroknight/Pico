#include "TestRunner.h"

#include "Pico/Core/App.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Math/Math.h"
#include "Pico/Core/Name.h"
#include "Pico/Core/Paths.h"
#include "Pico/Core/ProjectDescriptor.h"
#include "Pico/Core/Time.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace
{
void TestAssetPath(FTestRunner& Runner)
{
    Pico::FAssetPath Path;
    Pico::EAssetPathError Error = Pico::EAssetPathError::None;
    Runner.Expect(
        Pico::FAssetPath::TryParse("/Game/Models/Robot.pmesh", Path, &Error)
            && Error == Pico::EAssetPathError::None
            && Path.ToString() == "/Game/Models/Robot.pmesh"
            && Path.GetGameRelativePath() == "Models/Robot.pmesh"
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
        !Pico::FAssetPath::TryParse("/Game/Models/Robot", Path, &Error)
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

    Runner.Expect(
        Pico::FPaths::Init(ExecutablePath.string()),
        "Paths restore engine-only mode after project tests");
    std::filesystem::remove_all(TestRoot, ErrorCode);
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
}

int main(int Argc, char** Argv)
{
    Pico::FPaths::Init(Argc > 0 ? Argv[0] : "PicoCoreTests");

    FTestRunner Runner;
    TestAssetPath(Runner);
    TestCommandLine(Runner);
    TestConfig(Runner);
    TestPaths(Runner);
    TestAppOwnsProjectName(Runner);
    TestName(Runner);
    TestFrameTimer(Runner);
    TestVectorMath(Runner);
    TestRotationMath(Runner);
    TestTransformMath(Runner);
    return Runner.Finish();
}
