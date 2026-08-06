#include "TestRunner.h"

#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Core/Paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
Pico::FStaticMeshData MakeTriangleMesh(float Height = 1.0f)
{
    Pico::FStaticMeshData Mesh;
    Mesh.Vertices = {
        { Pico::FVector3(0.0f, 0.0f, 0.0f), Pico::FVector3::UpVector, {} },
        { Pico::FVector3(1.0f, 0.0f, 0.0f), Pico::FVector3::UpVector, Pico::FVector2(1.0f, 0.0f) },
        { Pico::FVector3(0.0f, 1.0f, Height), Pico::FVector3::UpVector, Pico::FVector2(0.0f, 1.0f) }
    };
    Mesh.Indices = { 0, 1, 2 };
    Mesh.Sections = { { 0, 3, "Default" } };
    Mesh.Bounds = { Pico::FVector3(0.0f), Pico::FVector3(1.0f, 1.0f, Height) };
    return Mesh;
}

void WriteFixture(const std::filesystem::path& Path, std::string_view Contents)
{
    std::filesystem::create_directories(Path.parent_path());
    std::ofstream File(Path, std::ios::binary | std::ios::trunc);
    File.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
}

void TestStaticMeshFormat(FTestRunner& Runner)
{
    const Pico::FStaticMeshData Mesh = MakeTriangleMesh();
    Pico::EStaticMeshError Error = Pico::EStaticMeshError::None;
    std::vector<Pico::uint8> FirstData;
    std::vector<Pico::uint8> SecondData;
    Runner.Expect(
        Pico::ValidateStaticMesh(Mesh, &Error)
            && Pico::SerializeStaticMesh(Mesh, FirstData, &Error)
            && Pico::SerializeStaticMesh(Mesh, SecondData, &Error)
            && FirstData == SecondData,
        "A valid static mesh produces deterministic .pmesh bytes");

    Pico::FStaticMeshData Loaded;
    Runner.Expect(
        Pico::DeserializeStaticMesh(FirstData, Loaded, &Error)
            && Loaded.Vertices.size() == Mesh.Vertices.size()
            && Loaded.Indices == Mesh.Indices
            && Loaded.Sections[0].MaterialSlotName == "Default"
            && Loaded.Bounds.Max.Equals(Mesh.Bounds.Max),
        "Static mesh data round trips with vertices, indices, sections and bounds");

    Pico::FStaticMeshData Invalid = Mesh;
    Invalid.Indices[2] = 99;
    Runner.Expect(
        !Pico::ValidateStaticMesh(Invalid, &Error)
            && Error == Pico::EStaticMeshError::InvalidData,
        "Static mesh validation rejects out-of-range indices");

    std::vector<Pico::uint8> Trailing = FirstData;
    Trailing.push_back(0xffu);
    Runner.Expect(
        !Pico::DeserializeStaticMesh(Trailing, Loaded, &Error)
            && Error == Pico::EStaticMeshError::TrailingData,
        "Static mesh loading rejects trailing bytes");

    std::vector<Pico::uint8> Unsupported = FirstData;
    Unsupported[4] = 2;
    Runner.Expect(
        !Pico::DeserializeStaticMesh(Unsupported, Loaded, &Error)
            && Error == Pico::EStaticMeshError::UnsupportedVersion,
        "Static mesh loading rejects unsupported format versions");
}

void TestAssetRegistry(FTestRunner& Runner, const char* Argv0)
{
    const std::filesystem::path EngineRoot = Pico::FPaths::GetEngineRootDir();
    const auto UniqueSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path TestRoot = std::filesystem::temp_directory_path()
        / ("PicoAssetTests_" + std::to_string(UniqueSuffix));
    const std::filesystem::path ProjectFile = TestRoot / "AssetTest.pico";
    std::error_code ErrorCode;
    std::filesystem::create_directories(TestRoot, ErrorCode);
    WriteFixture(
        ProjectFile,
        "[Project]\nName=AssetTest\nFileVersion=1\nEngineVersion=0.1.0\n");

    Runner.Expect(
        Pico::FPaths::Init(Argv0, ProjectFile),
        "Asset registry test initializes an external project");
    Pico::FAssetRegistry Registry;
    Pico::FAssetScanReport Report;
    Runner.Expect(
        Registry.ScanProjectContent(&Report)
            && Registry.GetAssets().empty()
            && Report.RegisteredAssetCount == 0,
        "A missing Content directory produces an empty registry");

    const std::filesystem::path Content = TestRoot / "Content";
    WriteFixture(Content / "Maps" / "Main.pworld", "world");
    const std::filesystem::path RobotPath = Content / "Models" / "Robot.pmesh";
    Pico::SaveStaticMeshToFile(RobotPath, MakeTriangleMesh());
    WriteFixture(Content / "Textures" / "Grid.ptex", "texture");
    WriteFixture(Content / "Materials" / "Metal.pmat", "material");
    WriteFixture(Content / "Source" / "Robot.obj", "source");

    Runner.Expect(
        Registry.ScanProjectContent(&Report)
            && Report.ScannedFileCount == 5
            && Report.RegisteredAssetCount == 4
            && Report.IgnoredFileCount == 1
            && Report.Issues.empty(),
        "Registry scans native assets and ignores import source files");
    const std::span<const Pico::FAssetRecord> Assets = Registry.GetAssets();
    Runner.Expect(
        Assets.size() == 4
            && Assets[0].AssetPath.ToString() == "/Game/Maps/Main.pworld"
            && Assets[1].AssetPath.ToString() == "/Game/Materials/Metal.pmat"
            && Assets[2].AssetPath.ToString() == "/Game/Models/Robot.pmesh"
            && Assets[3].AssetPath.ToString() == "/Game/Textures/Grid.ptex",
        "Registry results use deterministic virtual-path ordering");

    Pico::FAssetPath LowerCaseQuery;
    Pico::FAssetPath::TryParse("/Game/models/robot.pmesh", LowerCaseQuery);
    const Pico::FAssetRecord* Robot = Registry.Find(LowerCaseQuery);
    Runner.Expect(
        Robot != nullptr
            && Robot->Type == Pico::EAssetType::StaticMesh
            && Robot->FileSize > 0
            && Robot->FilePath == std::filesystem::weakly_canonical(
                RobotPath),
        "Registry resolves a virtual path to stable file metadata");

    Pico::FAssetManager AssetManager;
    Pico::EStaticMeshError MeshError = Pico::EStaticMeshError::None;
    const std::shared_ptr<const Pico::FStaticMeshData> FirstLoad =
        AssetManager.LoadStaticMesh(LowerCaseQuery, Registry, &MeshError);
    const std::shared_ptr<const Pico::FStaticMeshData> CachedLoad =
        AssetManager.LoadStaticMesh(LowerCaseQuery, Registry, &MeshError);
    Runner.Expect(
        FirstLoad != nullptr
            && CachedLoad == FirstLoad
            && AssetManager.GetCachedStaticMeshCount() == 1,
        "Asset manager reuses unchanged CPU static mesh data");

    Pico::SaveStaticMeshToFile(RobotPath, MakeTriangleMesh(2.0f));
    std::filesystem::last_write_time(
        RobotPath,
        std::filesystem::last_write_time(RobotPath) + std::chrono::seconds(2));
    Registry.ScanProjectContent(&Report);
    const std::shared_ptr<const Pico::FStaticMeshData> Reloaded =
        AssetManager.LoadStaticMesh(LowerCaseQuery, Registry, &MeshError);
    Runner.Expect(
        Reloaded != nullptr
            && Reloaded != FirstLoad
            && Reloaded->Bounds.Max.Z == 2.0f,
        "Asset manager reloads a static mesh after registry metadata changes");

    std::filesystem::remove(Content / "Models" / "Robot.pmesh", ErrorCode);
    Runner.Expect(
        Registry.ScanProjectContent(&Report)
            && Registry.Find(LowerCaseQuery) == nullptr
            && Registry.GetAssets().size() == 3,
        "A refresh atomically removes stale asset records");

    Runner.Expect(
        Pico::FPaths::Init(Argv0)
            && Registry.ScanProjectContent(&Report)
            && Registry.GetAssets().empty()
            && Pico::FPaths::GetEngineRootDir() == EngineRoot,
        "Engine-only mode clears project assets without losing the engine root");
    std::filesystem::remove_all(TestRoot, ErrorCode);
}
}

int main(int Argc, char** Argv)
{
    const char* Argv0 = Argc > 0 ? Argv[0] : "PicoAssetTests";
    Pico::FPaths::Init(Argv0);
    FTestRunner Runner;
    TestStaticMeshFormat(Runner);
    TestAssetRegistry(Runner, Argv0);
    return Runner.Finish();
}
