#include "TestRunner.h"

#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/CharacterProfile.h"
#include "Pico/Asset/Material.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Asset/Texture.h"
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

void TestCharacterProfile(FTestRunner& Runner)
{
    Pico::FCharacterProfileData Profile;
    Pico::FAssetPath::TryParse("/Game/Characters/Knight.pskeletalmesh", Profile.SkeletalMesh);
    Pico::FAssetPath::TryParse("/Game/Characters/Knight.panimset", Profile.AnimationSet);
    Pico::FAssetPath::TryParse("/Game/Characters/Materials/Armor.pmat", Profile.MaterialOverrides[0]);
    Pico::FAssetPath::TryParse("/Game/Characters/Materials/Cape.pmat", Profile.MaterialOverrides[7]);
    Profile.MeshTransform = Pico::FTransform(
        Pico::FRotator(0.0f, 180.0f, 0.0f),
        Pico::FVector3(0.0f, 0.0f, -96.0f),
        Pico::FVector3::OneVector);
    const std::filesystem::path File = std::filesystem::temp_directory_path()
        / "PicoCharacterProfileTest.pcharprofile";
    Pico::FCharacterProfileData Loaded;
    Pico::ECharacterProfileError Error = Pico::ECharacterProfileError::None;
    Runner.Expect(
        Pico::SaveCharacterProfileToFile(File, Profile, &Error)
            && Pico::LoadCharacterProfileFromFile(File, Loaded, &Error)
            && Loaded.SkeletalMesh == Profile.SkeletalMesh
            && Loaded.AnimationSet == Profile.AnimationSet
            && Loaded.MeshTransform.Equals(Profile.MeshTransform)
            && Loaded.MaterialOverrides[0] == Profile.MaterialOverrides[0]
            && Loaded.MaterialOverrides[7] == Profile.MaterialOverrides[7],
        "Character Profile round trips mesh, visual transform, animation, and all eight material slots");
    std::error_code FileError;
    std::filesystem::remove(File, FileError);
}

void TestTextureAndMaterialFormats(FTestRunner& Runner)
{
    Pico::FTextureData Texture;
    Texture.Width = 2;
    Texture.Height = 2;
    Texture.Pixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255};
    std::vector<Pico::uint8> TextureBytes;
    Pico::FTextureData LoadedTexture;
    Runner.Expect(
        Pico::SerializeTexture(Texture, TextureBytes)
            && Pico::DeserializeTexture(TextureBytes, LoadedTexture)
            && LoadedTexture.Width == 2
            && LoadedTexture.Height == 2
            && LoadedTexture.Pixels == Texture.Pixels,
        "RGBA8 texture data round trips through deterministic .ptex bytes");
    TextureBytes.push_back(0xffu);
    Runner.Expect(
        !Pico::DeserializeTexture(TextureBytes, LoadedTexture),
        "Texture loading rejects trailing bytes");

    Pico::FMaterialData Material;
    Material.BaseColor = Pico::FVector3(0.8f, 0.2f, 0.1f);
    Material.Metallic = 0.75f;
    Material.Roughness = 0.2f;
    Pico::FAssetPath::TryParse("/Game/Textures/Grid.ptex", Material.BaseColorTexture);
    std::vector<Pico::uint8> MaterialBytes;
    Pico::FMaterialData LoadedMaterial;
    Runner.Expect(
        Pico::SerializeMaterial(Material, MaterialBytes)
            && Pico::DeserializeMaterial(MaterialBytes, LoadedMaterial)
            && LoadedMaterial.BaseColor.Equals(Material.BaseColor)
            && LoadedMaterial.Metallic == Material.Metallic
            && LoadedMaterial.Roughness == Material.Roughness
            && LoadedMaterial.BaseColorTexture == Material.BaseColorTexture,
        "PBR material parameters and texture reference round trip through .pmat bytes");
    Material.Roughness = 0.0f;
    Runner.Expect(
        !Pico::ValidateMaterial(Material),
        "Material validation rejects out-of-range PBR parameters");
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
    const std::filesystem::path RobotPath = Content / "Meshes" / "Robot.pmesh";
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
            && Assets[2].AssetPath.ToString() == "/Game/Meshes/Robot.pmesh"
            && Assets[3].AssetPath.ToString() == "/Game/Textures/Grid.ptex",
        "Registry results use deterministic virtual-path ordering");

    Pico::FAssetPath LowerCaseQuery;
    Pico::FAssetPath::TryParse("/Game/meshes/robot.pmesh", LowerCaseQuery);
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

    std::filesystem::remove(RobotPath, ErrorCode);
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
    TestCharacterProfile(Runner);
    TestTextureAndMaterialFormats(Runner);
    TestAssetRegistry(Runner, Argv0);
    return Runner.Finish();
}
