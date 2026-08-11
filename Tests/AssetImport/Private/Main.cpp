#include "TestRunner.h"

#include "Pico/Asset/StaticMesh.h"
#include "Pico/AssetImport/StaticMeshImporter.h"
#include "Pico/AssetImport/SkeletalAnimationImporter.h"
#include "Pico/AssetImport/TextureImporter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
void TestObjImport(FTestRunner& Runner)
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("PicoAssetImportTests_" + std::to_string(Suffix));
    const std::filesystem::path Source = Root / "Quad.obj";
    const std::filesystem::path Destination = Root / "Quad.pmesh";
    std::filesystem::create_directories(Root);
    {
        std::ofstream File(Source, std::ios::trunc);
        File
            << "o Quad\n"
            << "v 0 0 0\n"
            << "v 1 0 0\n"
            << "v 1 1 0\n"
            << "v 0 1 0\n"
            << "vt 0 0\n"
            << "vt 1 0\n"
            << "vt 1 1\n"
            << "vt 0 1\n"
            << "f 1/1 2/2 3/3 4/4\n";
    }

    Pico::FStaticMeshImportResult Result;
    Pico::EStaticMeshImportError Error = Pico::EStaticMeshImportError::None;
    Runner.Expect(
        Pico::ImportObjStaticMesh(Source, {}, Result, &Error)
            && Error == Pico::EStaticMeshImportError::None,
        "TinyObjLoader imports an OBJ into validated Pico mesh data");
    Runner.Expect(
        Result.Mesh.Vertices.size() == 4
            && Result.Mesh.Indices.size() == 6
            && Result.Mesh.Sections.size() == 1,
        "OBJ polygons are triangulated with deterministic vertex reuse");
    Runner.Expect(
        Result.Mesh.Bounds.Min.Equals(Pico::FVector3(0.0f, 0.0f, 0.0f))
            && Result.Mesh.Bounds.Max.Equals(Pico::FVector3(1.0f, 0.0f, 1.0f))
            && Result.Mesh.Vertices[0].TexCoord.Equals(Pico::FVector2(0.0f, 1.0f)),
        "OBJ import converts Y-up coordinates and texture V consistently");
    Runner.Expect(
        !Result.Mesh.Vertices[0].Normal.IsNearlyZero(),
        "Missing OBJ normals are generated from triangle geometry");

    Runner.Expect(
        Pico::ImportObjStaticMeshToFile(Source, Destination, {}, &Error)
            && std::filesystem::is_regular_file(Destination),
        "The importer writes a native .pmesh file");
    Pico::FStaticMeshData Loaded;
    Runner.Expect(
        Pico::LoadStaticMeshFromFile(Destination, Loaded)
            && Loaded.Vertices.size() == Result.Mesh.Vertices.size()
            && Loaded.Indices == Result.Mesh.Indices,
        "An imported .pmesh is immediately loadable by the runtime format");

    Pico::FStaticMeshImportOptions InvalidOptions;
    InvalidOptions.UniformScale = 0.0f;
    Runner.Expect(
        !Pico::ImportObjStaticMesh(Source, InvalidOptions, Result, &Error)
            && Error == Pico::EStaticMeshImportError::InvalidArgument,
        "The importer rejects invalid coordinate conversion options");

    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);
}

void TestTextureImport(FTestRunner& Runner)
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("PicoTextureImportTests_" + std::to_string(Suffix));
    const std::filesystem::path Source = Root / "Colors.ppm";
    const std::filesystem::path Destination = Root / "Colors.ptex";
    std::filesystem::create_directories(Root);
    {
        std::ofstream File(Source, std::ios::binary | std::ios::trunc);
        File << "P6\n2 1\n255\n";
        const unsigned char Pixels[] = {255, 0, 0, 0, 255, 0};
        File.write(reinterpret_cast<const char*>(Pixels), sizeof(Pixels));
    }
    Pico::ETextureImportError Error = Pico::ETextureImportError::None;
    Pico::FTextureData Texture;
    Runner.Expect(
        Pico::ImportTexture(Source, Texture, &Error)
            && Texture.Width == 2
            && Texture.Height == 1
            && Texture.Pixels.size() == 8
            && Texture.Pixels[0] == 255
            && Texture.Pixels[5] == 255,
        "stb_image decodes source images into validated RGBA8 texture data");
    Runner.Expect(
        Pico::ImportTextureToFile(Source, Destination, &Error)
            && Pico::LoadTextureFromFile(Destination, Texture),
        "Texture importer writes a runtime-loadable .ptex asset");
    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);
}

#if defined(PICO_ASSIMP_GLTF_FIXTURE) && defined(PICO_ASSIMP_FBX_FIXTURE)
void TestSkeletalImport(FTestRunner& Runner)
{
    Pico::FAssetPath SkeletonAssetPath;
    Pico::FAssetPath::TryParse(
        "/Game/Characters/AssimpTest.pskeleton", SkeletonAssetPath);

    Pico::FSkeletalImportResult GltfResult;
    Pico::ESkeletalImportError Error = Pico::ESkeletalImportError::None;
    Runner.Expect(
        Pico::ImportSkeletalAnimation(
            PICO_ASSIMP_GLTF_FIXTURE, SkeletonAssetPath, {}, GltfResult, &Error)
            && Error == Pico::ESkeletalImportError::None
            && Pico::ValidateSkeleton(GltfResult.Skeleton)
            && Pico::ValidateSkeletalMesh(GltfResult.Mesh, &GltfResult.Skeleton)
            && !GltfResult.Animations.empty()
            && Pico::ValidateAnimationClip(
                GltfResult.Animations.front(), &GltfResult.Skeleton),
        "Assimp imports an animated glTF skin into validated Pico native data");

    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("PicoSkeletalImportTests_" + std::to_string(Suffix));
    std::filesystem::create_directories(Root / "Animations");
    Runner.Expect(
        Pico::SaveSkeletalImportResult(
            GltfResult,
            Root / "Test.pskeleton",
            Root / "Test.pskeletalmesh",
            Root / "Animations",
            &Error)
            && std::filesystem::is_regular_file(Root / "Test.pskeleton")
            && std::filesystem::is_regular_file(Root / "Test.pskeletalmesh")
            && !std::filesystem::is_empty(Root / "Animations"),
        "Imported glTF skeleton, mesh and clips save as runtime-loadable Pico assets");

    Pico::FSkeletalImportResult FbxResult;
    Runner.Expect(
        Pico::ImportSkeletalAnimation(
            PICO_ASSIMP_FBX_FIXTURE, SkeletonAssetPath, {}, FbxResult, &Error)
            && Error == Pico::ESkeletalImportError::None
            && Pico::ValidateSkeleton(FbxResult.Skeleton)
            && Pico::ValidateSkeletalMesh(FbxResult.Mesh, &FbxResult.Skeleton)
            && !FbxResult.Animations.empty(),
        "Assimp experimental FBX path imports a weighted mesh, skeleton and animation");

    std::error_code ErrorCode;
    std::filesystem::remove_all(Root, ErrorCode);
}
#endif
}

int main()
{
    FTestRunner Runner;
    TestObjImport(Runner);
    TestTextureImport(Runner);
#if defined(PICO_ASSIMP_GLTF_FIXTURE) && defined(PICO_ASSIMP_FBX_FIXTURE)
    TestSkeletalImport(Runner);
#endif
    return Runner.Finish();
}
