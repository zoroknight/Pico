#pragma once

#include "Pico/Core/Types.h"

#include <compare>
#include <cstddef>
#include <string>

namespace Pico
{
inline constexpr uint32 NetProtocolMagic = 0x5049434Fu;
inline constexpr uint16 NetProtocolVersion = 1;
inline constexpr std::size_t MaxNetDatagramBytes = 1200;

struct FNetConnectionId
{
    uint32 Value = 0;

    bool IsValid() const { return Value != 0; }
    auto operator<=>(const FNetConnectionId&) const = default;
};

struct FNetObjectId
{
    uint32 Value = 0;

    bool IsValid() const { return Value != 0; }
    auto operator<=>(const FNetObjectId&) const = default;
};

struct FNetAddress
{
    std::string Host;
    uint16 Port = 0;

    bool IsValid() const { return !Host.empty() && Port != 0; }
    std::string ToString() const;
    auto operator<=>(const FNetAddress&) const = default;
};

enum class ENetConnectionState : uint8
{
    Closed,
    Handshaking,
    Open,
    Closing
};

const char* ToString(ENetConnectionState State);

struct FNetStatistics
{
    uint64 PacketsSent = 0;
    uint64 PacketsReceived = 0;
    uint64 PacketsDropped = 0;
    uint64 DuplicatePackets = 0;
    uint64 OutOfOrderPackets = 0;
    uint64 ReliableMessagesSent = 0;
    uint64 ReliableMessagesDelivered = 0;
    uint64 ReliableMessagesResent = 0;
    double SmoothedRoundTripSeconds = 0.0;
};
}
