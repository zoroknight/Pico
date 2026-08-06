#pragma once

#include "Pico/Asset/StaticMesh.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Pico
{
enum class EStaticMeshImportError
{
    None,
    InvalidArgument,
    SourceOpenFailed,
    ParseFailed,
    EmptyMesh,
    InvalidMesh,
    SaveFailed
};

struct FStaticMeshImportOptions
{
    float UniformScale = 1.0f;
    bool bConvertYUpToZUp = true;
    bool bFlipTexCoordV = true;
};

struct FStaticMeshImportResult
{
    FStaticMeshData Mesh;
    std::string Warning;
};

bool ImportObjStaticMesh(
    const std::filesystem::path& SourceFile,
    const FStaticMeshImportOptions& Options,
    FStaticMeshImportResult& OutResult,
    EStaticMeshImportError* OutError = nullptr);
bool ImportObjStaticMeshToFile(
    const std::filesystem::path& SourceFile,
    const std::filesystem::path& DestinationFile,
    const FStaticMeshImportOptions& Options = {},
    EStaticMeshImportError* OutError = nullptr);

std::string_view ToString(EStaticMeshImportError Error);
}
