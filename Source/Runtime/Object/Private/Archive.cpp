#include "Pico/Object/Archive.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace Pico
{
namespace
{
constexpr uint32 MaxSerializedStringLength = 1024 * 1024;
}

FArchive::FArchive(EArchiveMode InMode)
    : Mode(InMode)
{
}

bool FArchive::IsSaving() const
{
    return Mode == EArchiveMode::Saving;
}

bool FArchive::IsLoading() const
{
    return Mode == EArchiveMode::Loading;
}

bool FArchive::HasError() const
{
    return bHasError;
}

void FArchive::SerializeUInt8(uint8& Value)
{
    SerializeBytes(&Value, sizeof(Value));
}

void FArchive::SerializeUInt32(uint32& Value)
{
    uint8 Bytes[sizeof(uint32)] {};
    if (IsSaving())
    {
        for (std::size_t Index = 0; Index < sizeof(uint32); ++Index)
        {
            Bytes[Index] = static_cast<uint8>((Value >> (Index * 8)) & 0xffu);
        }
    }

    SerializeBytes(Bytes, sizeof(Bytes));
    if (IsLoading() && !HasError())
    {
        Value = 0;
        for (std::size_t Index = 0; Index < sizeof(uint32); ++Index)
        {
            Value |= static_cast<uint32>(Bytes[Index]) << (Index * 8);
        }
    }
}

void FArchive::SerializeUInt64(uint64& Value)
{
    uint8 Bytes[sizeof(uint64)] {};
    if (IsSaving())
    {
        for (std::size_t Index = 0; Index < sizeof(uint64); ++Index)
        {
            Bytes[Index] = static_cast<uint8>((Value >> (Index * 8)) & 0xffu);
        }
    }

    SerializeBytes(Bytes, sizeof(Bytes));
    if (IsLoading() && !HasError())
    {
        Value = 0;
        for (std::size_t Index = 0; Index < sizeof(uint64); ++Index)
        {
            Value |= static_cast<uint64>(Bytes[Index]) << (Index * 8);
        }
    }
}

void FArchive::SerializeInt32(int32& Value)
{
    uint32 Bits = IsSaving() ? std::bit_cast<uint32>(Value) : 0;
    SerializeUInt32(Bits);
    if (IsLoading() && !HasError())
    {
        Value = std::bit_cast<int32>(Bits);
    }
}

void FArchive::SerializeFloat(float& Value)
{
    static_assert(sizeof(float) == sizeof(uint32));
    uint32 Bits = IsSaving() ? std::bit_cast<uint32>(Value) : 0;
    SerializeUInt32(Bits);
    if (IsLoading() && !HasError())
    {
        Value = std::bit_cast<float>(Bits);
    }
}

void FArchive::SerializeBool(bool& Value)
{
    uint8 SerializedValue = IsSaving() && Value ? 1 : 0;
    SerializeUInt8(SerializedValue);
    if (IsLoading() && !HasError())
    {
        if (SerializedValue > 1)
        {
            SetError();
            return;
        }

        Value = SerializedValue != 0;
    }
}

void FArchive::SerializeString(std::string& Value)
{
    if (IsSaving() && Value.size() > MaxSerializedStringLength)
    {
        SetError();
        return;
    }

    uint32 Length = IsSaving() ? static_cast<uint32>(Value.size()) : 0;
    SerializeUInt32(Length);
    if (HasError() || Length > MaxSerializedStringLength)
    {
        SetError();
        return;
    }

    if (IsLoading())
    {
        Value.resize(Length);
    }

    if (Length > 0)
    {
        SerializeBytes(Value.data(), Length);
    }
}

void FArchive::SerializeBytes(void* Data, std::size_t Size)
{
    if (HasError() || (Data == nullptr && Size > 0))
    {
        SetError();
        return;
    }

    SerializeRaw(Data, Size);
}

void FArchive::SetError()
{
    bHasError = true;
}

FMemoryWriter::FMemoryWriter()
    : FArchive(EArchiveMode::Saving)
{
}

const std::vector<uint8>& FMemoryWriter::GetData() const
{
    return Data;
}

void FMemoryWriter::SerializeRaw(void* Source, std::size_t Size)
{
    const auto* Bytes = static_cast<const uint8*>(Source);
    Data.insert(Data.end(), Bytes, Bytes + Size);
}

FMemoryReader::FMemoryReader(std::span<const uint8> InData)
    : FArchive(EArchiveMode::Loading)
    , Data(InData)
{
}

std::size_t FMemoryReader::GetRemainingSize() const
{
    return Data.size() - Offset;
}

void FMemoryReader::SerializeRaw(void* Destination, std::size_t Size)
{
    if (Size > GetRemainingSize())
    {
        SetError();
        return;
    }

    std::memcpy(Destination, Data.data() + Offset, Size);
    Offset += Size;
}
}
