#pragma once

#include "Pico/Net/NetTypes.h"

#include <span>
#include <vector>

namespace Pico
{
enum class ENetPacketFlags : uint16
{
    None = 0,
    Handshake = 1 << 0,
    Reliable = 1 << 1,
    Heartbeat = 1 << 2
};

constexpr ENetPacketFlags operator|(ENetPacketFlags Left, ENetPacketFlags Right)
{
    return static_cast<ENetPacketFlags>(
        static_cast<uint16>(Left) | static_cast<uint16>(Right));
}

constexpr bool HasAnyFlags(ENetPacketFlags Value, ENetPacketFlags Flags)
{
    return (static_cast<uint16>(Value) & static_cast<uint16>(Flags)) != 0;
}

enum class ENetHandshakeType : uint8
{
    ClientHello = 1,
    ServerWelcome = 2,
    ClientAck = 3
};

struct FNetPacketHeader
{
    uint32 Magic = NetProtocolMagic;
    uint16 Version = NetProtocolVersion;
    ENetPacketFlags Flags = ENetPacketFlags::None;
    FNetConnectionId ConnectionId;
    uint32 Sequence = 0;
    uint32 Ack = 0;
    uint32 AckBits = 0;
    uint16 PayloadBytes = 0;
};

struct FDecodedNetPacket
{
    FNetPacketHeader Header;
    std::vector<uint8> Payload;
};

bool IsNetSequenceNewer(uint32 Sequence, uint32 Reference);

class FNetByteWriter
{
public:
    bool WriteUInt8(uint8 Value);
    bool WriteUInt16(uint16 Value);
    bool WriteUInt32(uint32 Value);
    bool WriteUInt64(uint64 Value);
    bool WriteBytes(std::span<const uint8> Value);

    const std::vector<uint8>& GetBytes() const { return Bytes; }

private:
    std::vector<uint8> Bytes;
};

class FNetByteReader
{
public:
    explicit FNetByteReader(std::span<const uint8> InBytes)
        : Bytes(InBytes)
    {
    }

    bool ReadUInt8(uint8& OutValue);
    bool ReadUInt16(uint16& OutValue);
    bool ReadUInt32(uint32& OutValue);
    bool ReadUInt64(uint64& OutValue);
    bool ReadBytes(std::size_t Count, std::vector<uint8>& OutValue);
    std::size_t GetRemainingBytes() const { return Bytes.size() - Offset; }

private:
    std::span<const uint8> Bytes;
    std::size_t Offset = 0;
};

bool EncodeNetPacket(
    const FNetPacketHeader& Header,
    std::span<const uint8> Payload,
    std::vector<uint8>& OutBytes);
bool DecodeNetPacket(
    std::span<const uint8> Bytes,
    FDecodedNetPacket& OutPacket);
}
