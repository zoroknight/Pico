#include "Pico/Asset/Texture.h"

#include <fstream>
#include <limits>
#include <system_error>

namespace Pico
{
namespace
{
constexpr uint32 TextureMagic = 0x58455450;
constexpr uint32 TextureVersion = 1;
constexpr uint32 Rgba8Format = 1;
constexpr uint32 MaxTextureDimension = 16384;
constexpr std::size_t HeaderSize = sizeof(uint32) * 6;
constexpr std::size_t MaxTextureFileSize =
    HeaderSize + static_cast<std::size_t>(MaxTextureDimension) * MaxTextureDimension * 4;

void Report(ETextureError* OutError, ETextureError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

void WriteUInt32(std::vector<uint8>& Data, uint32 Value)
{
    for (std::size_t Index = 0; Index < sizeof(Value); ++Index)
    {
        Data.push_back(static_cast<uint8>((Value >> (Index * 8)) & 0xffu));
    }
}

bool ReadUInt32(std::span<const uint8> Data, std::size_t& Offset, uint32& OutValue)
{
    if (Data.size() - Offset < sizeof(uint32)) return false;
    OutValue = 0;
    for (std::size_t Index = 0; Index < sizeof(uint32); ++Index)
    {
        OutValue |= static_cast<uint32>(Data[Offset++]) << (Index * 8);
    }
    return true;
}

bool ReplaceFile(
    const std::filesystem::path& Temporary,
    const std::filesystem::path& Destination)
{
    std::error_code Error;
    std::filesystem::rename(Temporary, Destination, Error);
    if (!Error) return true;
    Error.clear();
    if (!std::filesystem::is_regular_file(Destination, Error)) return false;
    const std::filesystem::path Backup = Destination.string() + ".bak";
    std::filesystem::remove(Backup, Error);
    Error.clear();
    std::filesystem::rename(Destination, Backup, Error);
    if (Error) return false;
    std::filesystem::rename(Temporary, Destination, Error);
    if (Error)
    {
        std::error_code RestoreError;
        std::filesystem::rename(Backup, Destination, RestoreError);
        return false;
    }
    std::filesystem::remove(Backup, Error);
    return true;
}
}

bool ValidateTexture(const FTextureData& Texture, ETextureError* OutError)
{
    Report(OutError, ETextureError::None);
    if (Texture.Width == 0 || Texture.Height == 0)
    {
        Report(OutError, ETextureError::InvalidData);
        return false;
    }
    if (Texture.Width > MaxTextureDimension || Texture.Height > MaxTextureDimension)
    {
        Report(OutError, ETextureError::LimitExceeded);
        return false;
    }
    const std::size_t PixelCount =
        static_cast<std::size_t>(Texture.Width) * Texture.Height;
    if (PixelCount > std::numeric_limits<std::size_t>::max() / 4
        || Texture.Pixels.size() != PixelCount * 4)
    {
        Report(OutError, ETextureError::InvalidData);
        return false;
    }
    return true;
}

bool SerializeTexture(
    const FTextureData& Texture,
    std::vector<uint8>& OutData,
    ETextureError* OutError)
{
    OutData.clear();
    if (!ValidateTexture(Texture, OutError)) return false;
    OutData.reserve(HeaderSize + Texture.Pixels.size());
    WriteUInt32(OutData, TextureMagic);
    WriteUInt32(OutData, TextureVersion);
    WriteUInt32(OutData, Texture.Width);
    WriteUInt32(OutData, Texture.Height);
    WriteUInt32(OutData, Rgba8Format);
    WriteUInt32(OutData, static_cast<uint32>(Texture.Pixels.size()));
    OutData.insert(OutData.end(), Texture.Pixels.begin(), Texture.Pixels.end());
    return true;
}

bool DeserializeTexture(
    std::span<const uint8> Data,
    FTextureData& OutTexture,
    ETextureError* OutError)
{
    Report(OutError, ETextureError::None);
    std::size_t Offset = 0;
    uint32 Magic = 0;
    uint32 Version = 0;
    uint32 Format = 0;
    uint32 DataSize = 0;
    FTextureData Texture;
    if (!ReadUInt32(Data, Offset, Magic)
        || !ReadUInt32(Data, Offset, Version)
        || !ReadUInt32(Data, Offset, Texture.Width)
        || !ReadUInt32(Data, Offset, Texture.Height)
        || !ReadUInt32(Data, Offset, Format)
        || !ReadUInt32(Data, Offset, DataSize))
    {
        Report(OutError, ETextureError::InvalidArchive);
        return false;
    }
    if (Magic != TextureMagic)
    {
        Report(OutError, ETextureError::InvalidArchive);
        return false;
    }
    if (Version != TextureVersion)
    {
        Report(OutError, ETextureError::UnsupportedVersion);
        return false;
    }
    if (Format != Rgba8Format || Data.size() - Offset < DataSize)
    {
        Report(OutError, ETextureError::InvalidData);
        return false;
    }
    if (Data.size() - Offset != DataSize)
    {
        Report(OutError, ETextureError::TrailingData);
        return false;
    }
    Texture.Pixels.assign(Data.begin() + static_cast<std::ptrdiff_t>(Offset), Data.end());
    if (!ValidateTexture(Texture, OutError)) return false;
    OutTexture = std::move(Texture);
    return true;
}

bool SaveTextureToFile(
    const std::filesystem::path& FilePath,
    const FTextureData& Texture,
    ETextureError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, ETextureError::InvalidArgument);
        return false;
    }
    std::vector<uint8> Data;
    if (!SerializeTexture(Texture, Data, OutError)) return false;
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    const std::filesystem::path Temporary = FilePath.string() + ".tmp";
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        Report(OutError, ETextureError::FileOpenFailed);
        return false;
    }
    File.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
    File.close();
    if (!File)
    {
        std::filesystem::remove(Temporary, Error);
        Report(OutError, ETextureError::FileWriteFailed);
        return false;
    }
    if (!ReplaceFile(Temporary, FilePath))
    {
        std::filesystem::remove(Temporary, Error);
        Report(OutError, ETextureError::FileWriteFailed);
        return false;
    }
    return true;
}

bool LoadTextureFromFile(
    const std::filesystem::path& FilePath,
    FTextureData& OutTexture,
    ETextureError* OutError)
{
    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File)
    {
        Report(OutError, ETextureError::FileOpenFailed);
        return false;
    }
    const std::streamsize Size = File.tellg();
    if (Size < 0 || static_cast<std::uintmax_t>(Size) > MaxTextureFileSize)
    {
        Report(OutError, ETextureError::FileReadFailed);
        return false;
    }
    File.seekg(0);
    std::vector<uint8> Data(static_cast<std::size_t>(Size));
    if (Size > 0) File.read(reinterpret_cast<char*>(Data.data()), Size);
    if (!File)
    {
        Report(OutError, ETextureError::FileReadFailed);
        return false;
    }
    return DeserializeTexture(Data, OutTexture, OutError);
}

std::string_view ToString(ETextureError Error)
{
    switch (Error)
    {
    case ETextureError::None: return "None";
    case ETextureError::InvalidArgument: return "InvalidArgument";
    case ETextureError::InvalidData: return "InvalidData";
    case ETextureError::InvalidArchive: return "InvalidArchive";
    case ETextureError::UnsupportedVersion: return "UnsupportedVersion";
    case ETextureError::LimitExceeded: return "LimitExceeded";
    case ETextureError::FileOpenFailed: return "FileOpenFailed";
    case ETextureError::FileReadFailed: return "FileReadFailed";
    case ETextureError::FileWriteFailed: return "FileWriteFailed";
    case ETextureError::TrailingData: return "TrailingData";
    }
    return "Unknown";
}
}
