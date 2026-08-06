#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/Types.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace Pico
{
enum class EMaterialError
{
    None,
    InvalidArgument,
    InvalidData,
    InvalidArchive,
    UnsupportedVersion,
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    TrailingData
};

struct FMaterialData
{
    FVector3 BaseColor = FVector3(0.8f, 0.8f, 0.8f);
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    FAssetPath BaseColorTexture;
};

bool ValidateMaterial(const FMaterialData& Material, EMaterialError* OutError = nullptr);
bool SerializeMaterial(
    const FMaterialData& Material,
    std::vector<uint8>& OutData,
    EMaterialError* OutError = nullptr);
bool DeserializeMaterial(
    std::span<const uint8> Data,
    FMaterialData& OutMaterial,
    EMaterialError* OutError = nullptr);
bool SaveMaterialToFile(
    const std::filesystem::path& FilePath,
    const FMaterialData& Material,
    EMaterialError* OutError = nullptr);
bool LoadMaterialFromFile(
    const std::filesystem::path& FilePath,
    FMaterialData& OutMaterial,
    EMaterialError* OutError = nullptr);
std::string_view ToString(EMaterialError Error);
}
