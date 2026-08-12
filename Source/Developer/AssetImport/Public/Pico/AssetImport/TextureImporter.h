#pragma once

#include "Pico/Asset/Texture.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace Pico
{
enum class ETextureImportError
{
    None,
    InvalidArgument,
    DecodeFailed,
    InvalidTexture,
    SaveFailed
};

bool ImportTexture(
    const std::filesystem::path& SourceFile,
    FTextureData& OutTexture,
    ETextureImportError* OutError = nullptr);
bool ImportTextureMemory(
    std::span<const uint8> SourceData,
    FTextureData& OutTexture,
    ETextureImportError* OutError = nullptr);
bool ImportTextureToFile(
    const std::filesystem::path& SourceFile,
    const std::filesystem::path& DestinationFile,
    ETextureImportError* OutError = nullptr);
std::string_view ToString(ETextureImportError Error);
}
