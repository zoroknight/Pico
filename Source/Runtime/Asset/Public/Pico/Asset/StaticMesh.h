#pragma once

#include "Pico/Core/Math/Vector2.h"
#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/Types.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
enum class EStaticMeshError
{
    None,
    InvalidArgument,
    InvalidData,
    InvalidArchive,
    UnsupportedVersion,
    LimitExceeded,
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileTooLarge,
    TrailingData
};

struct FStaticMeshVertex
{
    FVector3 Position;
    FVector3 Normal;
    FVector2 TexCoord;
};

struct FStaticMeshSection
{
    uint32 FirstIndex = 0;
    uint32 IndexCount = 0;
    std::string MaterialSlotName;
};

struct FStaticMeshBounds
{
    FVector3 Min;
    FVector3 Max;
};

struct FStaticMeshData
{
    std::vector<FStaticMeshVertex> Vertices;
    std::vector<uint32> Indices;
    std::vector<FStaticMeshSection> Sections;
    FStaticMeshBounds Bounds;
};

bool ValidateStaticMesh(
    const FStaticMeshData& Mesh,
    EStaticMeshError* OutError = nullptr);
bool SerializeStaticMesh(
    const FStaticMeshData& Mesh,
    std::vector<uint8>& OutData,
    EStaticMeshError* OutError = nullptr);
bool DeserializeStaticMesh(
    std::span<const uint8> Data,
    FStaticMeshData& OutMesh,
    EStaticMeshError* OutError = nullptr);
bool SaveStaticMeshToFile(
    const std::filesystem::path& FilePath,
    const FStaticMeshData& Mesh,
    EStaticMeshError* OutError = nullptr);
bool LoadStaticMeshFromFile(
    const std::filesystem::path& FilePath,
    FStaticMeshData& OutMesh,
    EStaticMeshError* OutError = nullptr);

std::string_view ToString(EStaticMeshError Error);
}
