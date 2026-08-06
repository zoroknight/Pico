#include "TestRunner.h"

#include "Pico/Asset/StaticMesh.h"
#include "Pico/AssetImport/StaticMeshImporter.h"

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
}

int main()
{
    FTestRunner Runner;
    TestObjImport(Runner);
    return Runner.Finish();
}
