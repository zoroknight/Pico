#include "Pico/Net/NetPacket.h"

#include <limits>

namespace Pico
{
namespace
{
constexpr std::size_t NetPacketHeaderBytes = 26;
}

bool IsNetSequenceNewer(uint32 Sequence, uint32 Reference)
{
    return static_cast<int32>(Sequence - Reference) > 0;
}

bool FNetByteWriter::WriteUInt8(uint8 Value)
{
    if (Bytes.size() + 1 > MaxNetDatagramBytes) return false;
    Bytes.push_back(Value);
    return true;
}

bool FNetByteWriter::WriteUInt16(uint16 Value)
{
    return WriteUInt8(static_cast<uint8>((Value >> 8) & 0xff))
        && WriteUInt8(static_cast<uint8>(Value & 0xff));
}

bool FNetByteWriter::WriteUInt32(uint32 Value)
{
    return WriteUInt16(static_cast<uint16>((Value >> 16) & 0xffff))
        && WriteUInt16(static_cast<uint16>(Value & 0xffff));
}

bool FNetByteWriter::WriteUInt64(uint64 Value)
{
    return WriteUInt32(static_cast<uint32>((Value >> 32) & 0xffffffffu))
        && WriteUInt32(static_cast<uint32>(Value & 0xffffffffu));
}

bool FNetByteWriter::WriteBytes(std::span<const uint8> Value)
{
    if (Bytes.size() + Value.size() > MaxNetDatagramBytes) return false;
    Bytes.insert(Bytes.end(), Value.begin(), Value.end());
    return true;
}

bool FNetByteReader::ReadUInt8(uint8& OutValue)
{
    if (Offset >= Bytes.size()) return false;
    OutValue = Bytes[Offset++];
    return true;
}

bool FNetByteReader::ReadUInt16(uint16& OutValue)
{
    uint8 High = 0;
    uint8 Low = 0;
    if (!ReadUInt8(High) || !ReadUInt8(Low)) return false;
    OutValue = static_cast<uint16>((static_cast<uint16>(High) << 8) | Low);
    return true;
}

bool FNetByteReader::ReadUInt32(uint32& OutValue)
{
    uint16 High = 0;
    uint16 Low = 0;
    if (!ReadUInt16(High) || !ReadUInt16(Low)) return false;
    OutValue = (static_cast<uint32>(High) << 16) | Low;
    return true;
}

bool FNetByteReader::ReadUInt64(uint64& OutValue)
{
    uint32 High = 0;
    uint32 Low = 0;
    if (!ReadUInt32(High) || !ReadUInt32(Low)) return false;
    OutValue = (static_cast<uint64>(High) << 32) | Low;
    return true;
}

bool FNetByteReader::ReadBytes(std::size_t Count, std::vector<uint8>& OutValue)
{
    if (Count > GetRemainingBytes()) return false;
    OutValue.assign(Bytes.begin() + static_cast<std::ptrdiff_t>(Offset),
        Bytes.begin() + static_cast<std::ptrdiff_t>(Offset + Count));
    Offset += Count;
    return true;
}

bool EncodeNetPacket(
    const FNetPacketHeader& Header,
    std::span<const uint8> Payload,
    std::vector<uint8>& OutBytes)
{
    OutBytes.clear();
    if (Payload.size() > std::numeric_limits<uint16>::max()
        || NetPacketHeaderBytes + Payload.size() > MaxNetDatagramBytes)
    {
        return false;
    }

    FNetByteWriter Writer;
    const bool bWritten = Writer.WriteUInt32(Header.Magic)
        && Writer.WriteUInt16(Header.Version)
        && Writer.WriteUInt16(static_cast<uint16>(Header.Flags))
        && Writer.WriteUInt32(Header.ConnectionId.Value)
        && Writer.WriteUInt32(Header.Sequence)
        && Writer.WriteUInt32(Header.Ack)
        && Writer.WriteUInt32(Header.AckBits)
        && Writer.WriteUInt16(static_cast<uint16>(Payload.size()))
        && Writer.WriteBytes(Payload);
    if (!bWritten) return false;
    OutBytes = Writer.GetBytes();
    return true;
}

bool DecodeNetPacket(
    std::span<const uint8> Bytes,
    FDecodedNetPacket& OutPacket)
{
    OutPacket = {};
    if (Bytes.size() < NetPacketHeaderBytes
        || Bytes.size() > MaxNetDatagramBytes)
    {
        return false;
    }

    FNetByteReader Reader(Bytes);
    uint16 RawFlags = 0;
    uint32 ConnectionId = 0;
    if (!Reader.ReadUInt32(OutPacket.Header.Magic)
        || !Reader.ReadUInt16(OutPacket.Header.Version)
        || !Reader.ReadUInt16(RawFlags)
        || !Reader.ReadUInt32(ConnectionId)
        || !Reader.ReadUInt32(OutPacket.Header.Sequence)
        || !Reader.ReadUInt32(OutPacket.Header.Ack)
        || !Reader.ReadUInt32(OutPacket.Header.AckBits)
        || !Reader.ReadUInt16(OutPacket.Header.PayloadBytes))
    {
        return false;
    }
    OutPacket.Header.Flags = static_cast<ENetPacketFlags>(RawFlags);
    OutPacket.Header.ConnectionId.Value = ConnectionId;
    constexpr uint16 KnownFlags =
        static_cast<uint16>(ENetPacketFlags::Handshake)
        | static_cast<uint16>(ENetPacketFlags::Reliable)
        | static_cast<uint16>(ENetPacketFlags::Heartbeat);
    if (OutPacket.Header.Magic != NetProtocolMagic
        || OutPacket.Header.Version != NetProtocolVersion
        || (RawFlags & ~KnownFlags) != 0
        || Reader.GetRemainingBytes() != OutPacket.Header.PayloadBytes)
    {
        return false;
    }
    return Reader.ReadBytes(OutPacket.Header.PayloadBytes, OutPacket.Payload);
}
}
