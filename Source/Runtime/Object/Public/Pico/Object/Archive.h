#pragma once

#include "Pico/Core/Types.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace Pico
{
enum class EArchiveMode
{
    Saving,
    Loading
};

class FArchive
{
public:
    explicit FArchive(EArchiveMode InMode);
    virtual ~FArchive() = default;

    bool IsSaving() const;
    bool IsLoading() const;
    bool HasError() const;

    void SerializeUInt8(uint8& Value);
    void SerializeUInt32(uint32& Value);
    void SerializeInt32(int32& Value);
    void SerializeFloat(float& Value);
    void SerializeBool(bool& Value);
    void SerializeString(std::string& Value);
    void SerializeBytes(void* Data, std::size_t Size);

protected:
    void SetError();

private:
    virtual void SerializeRaw(void* Data, std::size_t Size) = 0;

    EArchiveMode Mode;
    bool bHasError = false;
};

class FMemoryWriter final : public FArchive
{
public:
    FMemoryWriter();

    const std::vector<uint8>& GetData() const;

private:
    void SerializeRaw(void* Data, std::size_t Size) override;

    std::vector<uint8> Data;
};

class FMemoryReader final : public FArchive
{
public:
    explicit FMemoryReader(std::span<const uint8> InData);

    std::size_t GetRemainingSize() const;

private:
    void SerializeRaw(void* Data, std::size_t Size) override;

    std::span<const uint8> Data;
    std::size_t Offset = 0;
};
}
