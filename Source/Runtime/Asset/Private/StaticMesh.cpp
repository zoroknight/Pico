#include "Pico/Asset/StaticMesh.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace Pico
{
namespace
{
constexpr uint32 StaticMeshMagic = 0x48534d50;
constexpr uint32 StaticMeshFormatVersion = 1;
constexpr uint32 MaxVertexCount = 16 * 1024 * 1024;
constexpr uint32 MaxIndexCount = 64 * 1024 * 1024;
constexpr uint32 MaxSectionCount = 64 * 1024;
constexpr uint32 MaxMaterialSlotLength = 1024;
constexpr std::size_t MaxStaticMeshFileSize = 512ull * 1024ull * 1024ull;

void ReportError(EStaticMeshError* OutError, EStaticMeshError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}

bool IsFinite(const FVector3& Value)
{
    return std::isfinite(Value.X)
        && std::isfinite(Value.Y)
        && std::isfinite(Value.Z);
}

bool IsFinite(const FVector2& Value)
{
    return std::isfinite(Value.X) && std::isfinite(Value.Y);
}

class FByteWriter
{
public:
    void UInt32(uint32 Value)
    {
        for (std::size_t Index = 0; Index < sizeof(Value); ++Index)
        {
            Data.push_back(static_cast<uint8>((Value >> (Index * 8)) & 0xffu));
        }
    }

    void Float(float Value)
    {
        UInt32(std::bit_cast<uint32>(Value));
    }

    bool String(const std::string& Value)
    {
        if (Value.size() > MaxMaterialSlotLength)
        {
            return false;
        }
        UInt32(static_cast<uint32>(Value.size()));
        Data.insert(Data.end(), Value.begin(), Value.end());
        return true;
    }

    std::vector<uint8> Take()
    {
        return std::move(Data);
    }

private:
    std::vector<uint8> Data;
};

class FByteReader
{
public:
    explicit FByteReader(std::span<const uint8> InData)
        : Data(InData)
    {
    }

    bool UInt32(uint32& OutValue)
    {
        if (Remaining() < sizeof(uint32))
        {
            return false;
        }
        OutValue = 0;
        for (std::size_t Index = 0; Index < sizeof(uint32); ++Index)
        {
            OutValue |= static_cast<uint32>(Data[Offset++]) << (Index * 8);
        }
        return true;
    }

    bool Float(float& OutValue)
    {
        uint32 Bits = 0;
        if (!UInt32(Bits))
        {
            return false;
        }
        OutValue = std::bit_cast<float>(Bits);
        return true;
    }

    bool String(std::string& OutValue)
    {
        uint32 Length = 0;
        if (!UInt32(Length)
            || Length > MaxMaterialSlotLength
            || Remaining() < Length)
        {
            return false;
        }
        OutValue.assign(
            reinterpret_cast<const char*>(Data.data() + Offset),
            Length);
        Offset += Length;
        return true;
    }

    std::size_t Remaining() const
    {
        return Data.size() - Offset;
    }

private:
    std::span<const uint8> Data;
    std::size_t Offset = 0;
};

bool ReplaceFile(
    const std::filesystem::path& TemporaryPath,
    const std::filesystem::path& FilePath)
{
    std::error_code ErrorCode;
    std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
    if (!ErrorCode)
    {
        return true;
    }
    ErrorCode.clear();
    if (!std::filesystem::is_regular_file(FilePath, ErrorCode))
    {
        return false;
    }
    std::filesystem::path BackupPath = FilePath;
    BackupPath += ".bak";
    std::filesystem::remove(BackupPath, ErrorCode);
    ErrorCode.clear();
    std::filesystem::rename(FilePath, BackupPath, ErrorCode);
    if (ErrorCode)
    {
        return false;
    }
    std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
    if (ErrorCode)
    {
        std::error_code RestoreError;
        std::filesystem::rename(BackupPath, FilePath, RestoreError);
        return false;
    }
    std::filesystem::remove(BackupPath, ErrorCode);
    return true;
}
}

bool ValidateStaticMesh(const FStaticMeshData& Mesh, EStaticMeshError* OutError)
{
    ReportError(OutError, EStaticMeshError::None);
    if (Mesh.Vertices.empty()
        || Mesh.Indices.empty()
        || Mesh.Sections.empty()
        || Mesh.Indices.size() % 3 != 0)
    {
        ReportError(OutError, EStaticMeshError::InvalidData);
        return false;
    }
    if (Mesh.Vertices.size() > MaxVertexCount
        || Mesh.Indices.size() > MaxIndexCount
        || Mesh.Sections.size() > MaxSectionCount)
    {
        ReportError(OutError, EStaticMeshError::LimitExceeded);
        return false;
    }
    for (const FStaticMeshVertex& Vertex : Mesh.Vertices)
    {
        if (!IsFinite(Vertex.Position)
            || !IsFinite(Vertex.Normal)
            || !IsFinite(Vertex.TexCoord))
        {
            ReportError(OutError, EStaticMeshError::InvalidData);
            return false;
        }
    }
    for (uint32 Index : Mesh.Indices)
    {
        if (Index >= Mesh.Vertices.size())
        {
            ReportError(OutError, EStaticMeshError::InvalidData);
            return false;
        }
    }
    uint32 ExpectedFirstIndex = 0;
    for (const FStaticMeshSection& Section : Mesh.Sections)
    {
        if (Section.FirstIndex != ExpectedFirstIndex
            || Section.IndexCount == 0
            || Section.IndexCount % 3 != 0
            || Section.FirstIndex > Mesh.Indices.size()
            || Section.IndexCount > Mesh.Indices.size() - Section.FirstIndex
            || Section.MaterialSlotName.size() > MaxMaterialSlotLength)
        {
            ReportError(OutError, EStaticMeshError::InvalidData);
            return false;
        }
        ExpectedFirstIndex += Section.IndexCount;
    }
    if (ExpectedFirstIndex != Mesh.Indices.size()
        || !IsFinite(Mesh.Bounds.Min)
        || !IsFinite(Mesh.Bounds.Max)
        || Mesh.Bounds.Min.X > Mesh.Bounds.Max.X
        || Mesh.Bounds.Min.Y > Mesh.Bounds.Max.Y
        || Mesh.Bounds.Min.Z > Mesh.Bounds.Max.Z)
    {
        ReportError(OutError, EStaticMeshError::InvalidData);
        return false;
    }
    return true;
}

bool SerializeStaticMesh(
    const FStaticMeshData& Mesh,
    std::vector<uint8>& OutData,
    EStaticMeshError* OutError)
{
    ReportError(OutError, EStaticMeshError::None);
    if (!ValidateStaticMesh(Mesh, OutError))
    {
        return false;
    }
    FByteWriter Writer;
    Writer.UInt32(StaticMeshMagic);
    Writer.UInt32(StaticMeshFormatVersion);
    Writer.UInt32(static_cast<uint32>(Mesh.Vertices.size()));
    Writer.UInt32(static_cast<uint32>(Mesh.Indices.size()));
    Writer.UInt32(static_cast<uint32>(Mesh.Sections.size()));
    for (const FVector3 Value : { Mesh.Bounds.Min, Mesh.Bounds.Max })
    {
        Writer.Float(Value.X);
        Writer.Float(Value.Y);
        Writer.Float(Value.Z);
    }
    for (const FStaticMeshVertex& Vertex : Mesh.Vertices)
    {
        Writer.Float(Vertex.Position.X);
        Writer.Float(Vertex.Position.Y);
        Writer.Float(Vertex.Position.Z);
        Writer.Float(Vertex.Normal.X);
        Writer.Float(Vertex.Normal.Y);
        Writer.Float(Vertex.Normal.Z);
        Writer.Float(Vertex.TexCoord.X);
        Writer.Float(Vertex.TexCoord.Y);
    }
    for (uint32 Index : Mesh.Indices)
    {
        Writer.UInt32(Index);
    }
    for (const FStaticMeshSection& Section : Mesh.Sections)
    {
        Writer.UInt32(Section.FirstIndex);
        Writer.UInt32(Section.IndexCount);
        if (!Writer.String(Section.MaterialSlotName))
        {
            ReportError(OutError, EStaticMeshError::InvalidData);
            return false;
        }
    }
    OutData = Writer.Take();
    return true;
}

bool DeserializeStaticMesh(
    std::span<const uint8> Data,
    FStaticMeshData& OutMesh,
    EStaticMeshError* OutError)
{
    ReportError(OutError, EStaticMeshError::None);
    FByteReader Reader(Data);
    uint32 Magic = 0;
    uint32 Version = 0;
    uint32 VertexCount = 0;
    uint32 IndexCount = 0;
    uint32 SectionCount = 0;
    if (!Reader.UInt32(Magic)
        || !Reader.UInt32(Version)
        || !Reader.UInt32(VertexCount)
        || !Reader.UInt32(IndexCount)
        || !Reader.UInt32(SectionCount))
    {
        ReportError(OutError, EStaticMeshError::InvalidArchive);
        return false;
    }
    if (Magic != StaticMeshMagic)
    {
        ReportError(OutError, EStaticMeshError::InvalidArchive);
        return false;
    }
    if (Version != StaticMeshFormatVersion)
    {
        ReportError(OutError, EStaticMeshError::UnsupportedVersion);
        return false;
    }
    if (VertexCount == 0
        || VertexCount > MaxVertexCount
        || IndexCount == 0
        || IndexCount > MaxIndexCount
        || SectionCount == 0
        || SectionCount > MaxSectionCount)
    {
        ReportError(OutError, EStaticMeshError::LimitExceeded);
        return false;
    }

    FStaticMeshData Mesh;
    for (FVector3* Value : { &Mesh.Bounds.Min, &Mesh.Bounds.Max })
    {
        if (!Reader.Float(Value->X)
            || !Reader.Float(Value->Y)
            || !Reader.Float(Value->Z))
        {
            ReportError(OutError, EStaticMeshError::InvalidArchive);
            return false;
        }
    }
    constexpr std::size_t SerializedVertexSize = sizeof(float) * 8;
    constexpr std::size_t MinimumSerializedSectionSize = sizeof(uint32) * 3;
    const std::size_t MinimumPayloadSize =
        static_cast<std::size_t>(VertexCount) * SerializedVertexSize
        + static_cast<std::size_t>(IndexCount) * sizeof(uint32)
        + static_cast<std::size_t>(SectionCount) * MinimumSerializedSectionSize;
    if (Reader.Remaining() < MinimumPayloadSize)
    {
        ReportError(OutError, EStaticMeshError::InvalidArchive);
        return false;
    }
    Mesh.Vertices.resize(VertexCount);
    for (FStaticMeshVertex& Vertex : Mesh.Vertices)
    {
        if (!Reader.Float(Vertex.Position.X)
            || !Reader.Float(Vertex.Position.Y)
            || !Reader.Float(Vertex.Position.Z)
            || !Reader.Float(Vertex.Normal.X)
            || !Reader.Float(Vertex.Normal.Y)
            || !Reader.Float(Vertex.Normal.Z)
            || !Reader.Float(Vertex.TexCoord.X)
            || !Reader.Float(Vertex.TexCoord.Y))
        {
            ReportError(OutError, EStaticMeshError::InvalidArchive);
            return false;
        }
    }
    Mesh.Indices.resize(IndexCount);
    for (uint32& Index : Mesh.Indices)
    {
        if (!Reader.UInt32(Index))
        {
            ReportError(OutError, EStaticMeshError::InvalidArchive);
            return false;
        }
    }
    Mesh.Sections.resize(SectionCount);
    for (FStaticMeshSection& Section : Mesh.Sections)
    {
        if (!Reader.UInt32(Section.FirstIndex)
            || !Reader.UInt32(Section.IndexCount)
            || !Reader.String(Section.MaterialSlotName))
        {
            ReportError(OutError, EStaticMeshError::InvalidArchive);
            return false;
        }
    }
    if (Reader.Remaining() != 0)
    {
        ReportError(OutError, EStaticMeshError::TrailingData);
        return false;
    }
    if (!ValidateStaticMesh(Mesh, OutError))
    {
        return false;
    }
    OutMesh = std::move(Mesh);
    return true;
}

bool SaveStaticMeshToFile(
    const std::filesystem::path& FilePath,
    const FStaticMeshData& Mesh,
    EStaticMeshError* OutError)
{
    if (FilePath.empty())
    {
        ReportError(OutError, EStaticMeshError::InvalidArgument);
        return false;
    }
    std::vector<uint8> Data;
    if (!SerializeStaticMesh(Mesh, Data, OutError))
    {
        return false;
    }
    std::error_code ErrorCode;
    std::filesystem::create_directories(FilePath.parent_path(), ErrorCode);
    if (ErrorCode)
    {
        ReportError(OutError, EStaticMeshError::FileWriteFailed);
        return false;
    }
    std::filesystem::path TemporaryPath = FilePath;
    TemporaryPath += ".tmp";
    {
        std::ofstream File(TemporaryPath, std::ios::binary | std::ios::trunc);
        File.write(
            reinterpret_cast<const char*>(Data.data()),
            static_cast<std::streamsize>(Data.size()));
        if (!File.good())
        {
            ReportError(OutError, EStaticMeshError::FileWriteFailed);
            return false;
        }
    }
    if (!ReplaceFile(TemporaryPath, FilePath))
    {
        ReportError(OutError, EStaticMeshError::FileWriteFailed);
        return false;
    }
    ReportError(OutError, EStaticMeshError::None);
    return true;
}

bool LoadStaticMeshFromFile(
    const std::filesystem::path& FilePath,
    FStaticMeshData& OutMesh,
    EStaticMeshError* OutError)
{
    ReportError(OutError, EStaticMeshError::None);
    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File)
    {
        ReportError(OutError, EStaticMeshError::FileOpenFailed);
        return false;
    }
    const std::streampos End = File.tellg();
    if (End < 0)
    {
        ReportError(OutError, EStaticMeshError::FileReadFailed);
        return false;
    }
    const auto Size = static_cast<std::size_t>(End);
    if (Size > MaxStaticMeshFileSize)
    {
        ReportError(OutError, EStaticMeshError::FileTooLarge);
        return false;
    }
    std::vector<uint8> Data(Size);
    File.seekg(0, std::ios::beg);
    if (Size > 0)
    {
        File.read(
            reinterpret_cast<char*>(Data.data()),
            static_cast<std::streamsize>(Size));
    }
    if (!File)
    {
        ReportError(OutError, EStaticMeshError::FileReadFailed);
        return false;
    }
    return DeserializeStaticMesh(Data, OutMesh, OutError);
}

std::string_view ToString(EStaticMeshError Error)
{
    switch (Error)
    {
    case EStaticMeshError::None: return "None";
    case EStaticMeshError::InvalidArgument: return "InvalidArgument";
    case EStaticMeshError::InvalidData: return "InvalidData";
    case EStaticMeshError::InvalidArchive: return "InvalidArchive";
    case EStaticMeshError::UnsupportedVersion: return "UnsupportedVersion";
    case EStaticMeshError::LimitExceeded: return "LimitExceeded";
    case EStaticMeshError::FileOpenFailed: return "FileOpenFailed";
    case EStaticMeshError::FileReadFailed: return "FileReadFailed";
    case EStaticMeshError::FileWriteFailed: return "FileWriteFailed";
    case EStaticMeshError::FileTooLarge: return "FileTooLarge";
    case EStaticMeshError::TrailingData: return "TrailingData";
    }
    return "Unknown";
}
}
