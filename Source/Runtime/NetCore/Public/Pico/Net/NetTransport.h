#pragma once

#include "Pico/Net/NetTypes.h"

#include <span>
#include <string>
#include <vector>

namespace Pico
{
enum class ENetReceiveResult : uint8
{
    None,
    Packet,
    Error
};

class INetTransport
{
public:
    virtual ~INetTransport() = default;

    virtual bool Open(const FNetAddress& LocalAddress) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    virtual bool SendTo(
        const FNetAddress& RemoteAddress,
        std::span<const uint8> Bytes) = 0;
    virtual ENetReceiveResult ReceiveFrom(
        FNetAddress& OutRemoteAddress,
        std::vector<uint8>& OutBytes) = 0;
    virtual const FNetAddress& GetLocalAddress() const = 0;
    virtual const std::string& GetLastError() const = 0;
};
}
