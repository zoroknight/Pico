#include "Pico/Packaging/Packaging.h"
#include "Pico/Graph/GraphAsset.h"

#include "TestRunner.h"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace
{
bool WriteFile(
    const std::filesystem::path& FilePath,
    std::string_view Contents)
{
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    if (Error) return false;
    std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
    File << Contents;
    return static_cast<bool>(File);
}

void TestDevelopmentPackage(FTestRunner& Runner)
{
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / "PicoPackagingTests";
    const std::filesystem::path Engine = Root / "EngineSource";
    const std::filesystem::path Project = Root / "LearningGame";
    const std::filesystem::path Build = Root / "Build/Release";
    const std::filesystem::path Output = Root / "Output";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);

    Pico::FPicoGraphAsset Graph;
    Graph.GraphId = Pico::CreateGraphStableId();
    Graph.Nodes.push_back(Pico::MakeEntryEventNode("BeginPlay", 0.0f, 0.0f));
    Pico::EGraphAssetError GraphError = Pico::EGraphAssetError::None;

    Runner.Expect(
        WriteFile(Engine / "Config/Pico.ini", "[Engine]\nMaxFPS=60\n")
            && WriteFile(
                Project / "LearningGame.pico",
                "[Project]\nName=LearningGame\nFileVersion=1\nEngineVersion=0.1.0\n")
            && WriteFile(
                Project / "Config/Pico.ini",
                "[Game]\nDefaultMap=/Game/Maps/Main.pworld\nExecutable=FakeGame\n")
            && WriteFile(Project / "Content/Maps/Main.pworld", "world")
            && WriteFile(Project / "Content/Materials/M_Test.pmat", "material")
            && Pico::SaveGraphAssetToFile(
                Project / "Content/Graphs/BeginPlay.pgraph", Graph, &GraphError)
            && WriteFile(Project / "Content/Source/Test/model.fbx", "source")
            && WriteFile(Project / "Content/Source/Test/Preview.pmat", "source material")
            && WriteFile(Project / "Content/Notes.txt", "editor note")
            && WriteFile(Build / "FakeGame.exe", "executable")
            && WriteFile(Build / "Backend.dll", "backend")
            && WriteFile(
                Build / "FakeGame.targetreceipt",
                "[Target]\nName=FakeGame\nType=Game\nPlatform=Windows\n"
                "Configuration=Release\nEngineVersion=0.1.0\n"
                "Executable=FakeGame.exe\n\n[RuntimeDependencies]\n"
                "Dependency0=Backend.dll|Binaries/Backend.dll|PhysicsBackend\n"),
        "Packaging fixture creates Engine, project, native assets and Target Receipt");

    Pico::FPackageRequest Request;
    Request.EngineRoot = Engine;
    Request.ProjectFile = Project / "LearningGame.pico";
    Request.TargetReceiptFile = Build / "FakeGame.targetreceipt";
    Request.OutputRoot = Output;
    Pico::FPackageBuilder Builder;
    const Pico::FPackageResult Result = Builder.Build(Request);
    const std::filesystem::path Stage =
        Output / "LearningGame-Windows-Development";
    Runner.Expect(
        Result.bSucceeded && Result.StageRoot == Stage,
        "Development package commits a deterministic Stage directory");
    Runner.Expect(
        std::filesystem::is_regular_file(Stage / "Binaries/FakeGame.exe")
            && std::filesystem::is_regular_file(Stage / "Binaries/Backend.dll")
            && std::filesystem::is_regular_file(Stage / "Engine/PicoEngine.root")
            && std::filesystem::is_regular_file(Stage / "PicoStage.manifest")
            && std::filesystem::is_regular_file(Stage / "PackageReport.ini")
            && std::filesystem::is_regular_file(Stage / "PicoPackage.complete"),
        "Stage includes target, runtime dependency, identity manifests and report");
    Runner.Expect(
        std::filesystem::is_regular_file(
            Stage / "LearningGame/Content/Maps/Main.pworld")
            && std::filesystem::is_regular_file(
                Stage / "LearningGame/Content/Materials/M_Test.pmat"),
        "Stage includes every registered Pico native asset type");
    Runner.Expect(
        !std::filesystem::exists(
            Stage / "LearningGame/Content/Graphs/BeginPlay.pgraph")
            && std::filesystem::is_regular_file(
                Stage / "LearningGame/Content/Graphs/BeginPlay.pgraph.pgrb"),
        "Packaging cooks PicoGraph JSON into Runtime PGRB and excludes editor source");
    Runner.Expect(
        !std::filesystem::exists(Stage / "LearningGame/Content/Source")
            && !std::filesystem::exists(Stage / "LearningGame/Content/Notes.txt"),
        "Stage excludes import sources and unregistered editor files");

    Runner.Expect(
        WriteFile(Project / "Content/Maps/Main.pworld", "updated world"),
        "Packaging fixture can update a staged asset");
    const Pico::FPackageResult Rebuilt = Builder.Build(Request);
    std::ifstream RebuiltWorld(Stage / "LearningGame/Content/Maps/Main.pworld");
    std::string RebuiltWorldContents;
    std::getline(RebuiltWorld, RebuiltWorldContents);
    std::size_t VisibleStageCount = 0;
    for (const std::filesystem::directory_entry& Entry :
        std::filesystem::directory_iterator(Output))
    {
        if (Entry.is_directory()) ++VisibleStageCount;
    }
    Runner.Expect(
        Rebuilt.bSucceeded && RebuiltWorldContents == "updated world",
        "A repeated package atomically replaces the previous successful Stage");
    Runner.Expect(
        VisibleStageCount == 1,
        "A successful package leaves one final Stage and no temporary duplicate");

    Pico::FPackageRequest RenamedRequest = Request;
    RenamedRequest.StageNameOverride = "LearningGame-Copy";
    const Pico::FPackageResult Renamed = Builder.Build(RenamedRequest);
    Runner.Expect(
        Renamed.bSucceeded
            && Renamed.StageRoot == Output / "LearningGame-Copy"
            && std::filesystem::is_regular_file(
                Renamed.StageRoot / "LearningGame/LearningGame.pico"),
        "A package can use a distinct output name without changing project identity");

    Pico::FPackageRequest ShippingRequest = Request;
    ShippingRequest.Profile = Pico::EPackageProfile::Shipping;
    const Pico::FPackageResult Shipping = Builder.Build(ShippingRequest);
    Runner.Expect(
        !Shipping.bSucceeded
            && std::filesystem::is_regular_file(Stage / "Binaries/FakeGame.exe"),
        "Unimplemented Shipping labeling is rejected without changing Development output");

    Runner.Expect(
        WriteFile(
            Build / "FakeGame.targetreceipt",
            "[Target]\nName=FakeGame\nType=Game\nPlatform=Windows\n"
            "Configuration=Release\nEngineVersion=0.1.0\n"
            "Executable=MissingGame.exe\n"),
        "Packaging fixture can introduce a failed target build");
    const Pico::FPackageResult Failed = Builder.Build(Request);
    Runner.Expect(
        !Failed.bSucceeded
            && std::filesystem::is_regular_file(Stage / "Binaries/FakeGame.exe"),
        "A failed package preserves the previous successful Stage");

    std::filesystem::remove_all(Root, Error);
}
}

int main()
{
    FTestRunner Runner;
    TestDevelopmentPackage(Runner);
    return Runner.Finish();
}
