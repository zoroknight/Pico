#pragma once

#include "Pico/Core/Types.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace Pico
{
enum class ETextureError
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
    TrailingData
};

struct FTextureData
{
    uint32 Width = 0;
    uint32 Height = 0;
    std::vector<uint8> Pixels;
};

bool ValidateTexture(const FTextureData& Texture, ETextureError* OutError = nullptr);
bool SerializeTexture(
    const FTextureData& Texture,
    std::vector<uint8>& OutData,
    ETextureError* OutError = nullptr);
bool DeserializeTexture(
    std::span<const uint8> Data,
    FTextureData& OutTexture,
    ETextureError* OutError = nullptr);
bool SaveTextureToFile(
    const std::filesystem::path& FilePath,
    const FTextureData& Texture,
    ETextureError* OutError = nullptr);
bool LoadTextureFromFile(
    const std::filesystem::path& FilePath,
    FTextureData& OutTexture,
    ETextureError* OutError = nullptr);
std::string_view ToString(ETextureError Error);
}
