#include "Pico/Asset/Material.h"

#include <bit>
#include <cmath>
#include <fstream>
#include <system_error>

namespace Pico
{
namespace
{
constexpr uint32 MaterialMagic = 0x54414d50;
constexpr uint32 MaterialVersion = 1;
constexpr uint32 MaxPathLength = 4096;
constexpr std::size_t MaxMaterialFileSize = 64 * 1024;

void Report(EMaterialError* OutError, EMaterialError Error)
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

void WriteFloat(std::vector<uint8>& Data, float Value)
{
    WriteUInt32(Data, std::bit_cast<uint32>(Value));
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

bool ReadFloat(std::span<const uint8> Data, std::size_t& Offset, float& OutValue)
{
    uint32 Value = 0;
    if (!ReadUInt32(Data, Offset, Value)) return false;
    OutValue = std::bit_cast<float>(Value);
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

bool ValidateMaterial(const FMaterialData& Material, EMaterialError* OutError)
{
    Report(OutError, EMaterialError::None);
    const bool bFinite = std::isfinite(Material.BaseColor.X)
        && std::isfinite(Material.BaseColor.Y)
        && std::isfinite(Material.BaseColor.Z)
        && std::isfinite(Material.Metallic)
        && std::isfinite(Material.Roughness);
    const bool bInRange = Material.BaseColor.X >= 0.0f && Material.BaseColor.X <= 1.0f
        && Material.BaseColor.Y >= 0.0f && Material.BaseColor.Y <= 1.0f
        && Material.BaseColor.Z >= 0.0f && Material.BaseColor.Z <= 1.0f
        && Material.Metallic >= 0.0f && Material.Metallic <= 1.0f
        && Material.Roughness >= 0.04f && Material.Roughness <= 1.0f;
    if (!bFinite || !bInRange)
    {
        Report(OutError, EMaterialError::InvalidData);
        return false;
    }
    if (Material.BaseColorTexture.IsValid()
        && Material.BaseColorTexture.GetExtension() != ".ptex")
    {
        Report(OutError, EMaterialError::InvalidData);
        return false;
    }
    return true;
}

bool SerializeMaterial(
    const FMaterialData& Material,
    std::vector<uint8>& OutData,
    EMaterialError* OutError)
{
    OutData.clear();
    if (!ValidateMaterial(Material, OutError)) return false;
    const std::string Path(Material.BaseColorTexture.ToString());
    if (Path.size() > MaxPathLength)
    {
        Report(OutError, EMaterialError::InvalidData);
        return false;
    }
    WriteUInt32(OutData, MaterialMagic);
    WriteUInt32(OutData, MaterialVersion);
    WriteFloat(OutData, Material.BaseColor.X);
    WriteFloat(OutData, Material.BaseColor.Y);
    WriteFloat(OutData, Material.BaseColor.Z);
    WriteFloat(OutData, Material.Metallic);
    WriteFloat(OutData, Material.Roughness);
    WriteUInt32(OutData, static_cast<uint32>(Path.size()));
    OutData.insert(OutData.end(), Path.begin(), Path.end());
    return true;
}

bool DeserializeMaterial(
    std::span<const uint8> Data,
    FMaterialData& OutMaterial,
    EMaterialError* OutError)
{
    Report(OutError, EMaterialError::None);
    std::size_t Offset = 0;
    uint32 Magic = 0;
    uint32 Version = 0;
    uint32 PathLength = 0;
    FMaterialData Material;
    if (!ReadUInt32(Data, Offset, Magic)
        || !ReadUInt32(Data, Offset, Version)
        || !ReadFloat(Data, Offset, Material.BaseColor.X)
        || !ReadFloat(Data, Offset, Material.BaseColor.Y)
        || !ReadFloat(Data, Offset, Material.BaseColor.Z)
        || !ReadFloat(Data, Offset, Material.Metallic)
        || !ReadFloat(Data, Offset, Material.Roughness)
        || !ReadUInt32(Data, Offset, PathLength))
    {
        Report(OutError, EMaterialError::InvalidArchive);
        return false;
    }
    if (Magic != MaterialMagic)
    {
        Report(OutError, EMaterialError::InvalidArchive);
        return false;
    }
    if (Version != MaterialVersion)
    {
        Report(OutError, EMaterialError::UnsupportedVersion);
        return false;
    }
    if (PathLength > MaxPathLength || Data.size() - Offset < PathLength)
    {
        Report(OutError, EMaterialError::InvalidData);
        return false;
    }
    if (Data.size() - Offset != PathLength)
    {
        Report(OutError, EMaterialError::TrailingData);
        return false;
    }
    if (PathLength > 0)
    {
        const std::string Path(
            reinterpret_cast<const char*>(Data.data() + Offset), PathLength);
        if (!FAssetPath::TryParse(Path, Material.BaseColorTexture))
        {
            Report(OutError, EMaterialError::InvalidData);
            return false;
        }
    }
    if (!ValidateMaterial(Material, OutError)) return false;
    OutMaterial = std::move(Material);
    return true;
}

bool SaveMaterialToFile(
    const std::filesystem::path& FilePath,
    const FMaterialData& Material,
    EMaterialError* OutError)
{
    if (FilePath.empty())
    {
        Report(OutError, EMaterialError::InvalidArgument);
        return false;
    }
    std::vector<uint8> Data;
    if (!SerializeMaterial(Material, Data, OutError)) return false;
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    const std::filesystem::path Temporary = FilePath.string() + ".tmp";
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        Report(OutError, EMaterialError::FileOpenFailed);
        return false;
    }
    File.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
    File.close();
    if (!File)
    {
        std::filesystem::remove(Temporary, Error);
        Report(OutError, EMaterialError::FileWriteFailed);
        return false;
    }
    if (!ReplaceFile(Temporary, FilePath))
    {
        std::filesystem::remove(Temporary, Error);
        Report(OutError, EMaterialError::FileWriteFailed);
        return false;
    }
    return true;
}

bool LoadMaterialFromFile(
    const std::filesystem::path& FilePath,
    FMaterialData& OutMaterial,
    EMaterialError* OutError)
{
    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File)
    {
        Report(OutError, EMaterialError::FileOpenFailed);
        return false;
    }
    const std::streamsize Size = File.tellg();
    if (Size < 0 || static_cast<std::uintmax_t>(Size) > MaxMaterialFileSize)
    {
        Report(OutError, EMaterialError::FileReadFailed);
        return false;
    }
    File.seekg(0);
    std::vector<uint8> Data(static_cast<std::size_t>(Size));
    if (Size > 0) File.read(reinterpret_cast<char*>(Data.data()), Size);
    if (!File)
    {
        Report(OutError, EMaterialError::FileReadFailed);
        return false;
    }
    return DeserializeMaterial(Data, OutMaterial, OutError);
}

std::string_view ToString(EMaterialError Error)
{
    switch (Error)
    {
    case EMaterialError::None: return "None";
    case EMaterialError::InvalidArgument: return "InvalidArgument";
    case EMaterialError::InvalidData: return "InvalidData";
    case EMaterialError::InvalidArchive: return "InvalidArchive";
    case EMaterialError::UnsupportedVersion: return "UnsupportedVersion";
    case EMaterialError::FileOpenFailed: return "FileOpenFailed";
    case EMaterialError::FileReadFailed: return "FileReadFailed";
    case EMaterialError::FileWriteFailed: return "FileWriteFailed";
    case EMaterialError::TrailingData: return "TrailingData";
    }
    return "Unknown";
}
}
