#pragma once

#include "Pico/Net/NetTransport.h"

namespace Pico
{
class FUdpTransport final : public INetTransport
{
public:
    FUdpTransport();
    ~FUdpTransport() override;

    bool Open(const FNetAddress& LocalAddress) override;
    void Close() override;
    bool IsOpen() const override;
    bool SendTo(
        const FNetAddress& RemoteAddress,
        std::span<const uint8> Bytes) override;
    ENetReceiveResult ReceiveFrom(
        FNetAddress& OutRemoteAddress,
        std::vector<uint8>& OutBytes) override;
    const FNetAddress& GetLocalAddress() const override;
    const std::string& GetLastError() const override;

private:
    void SetSocketError(std::string_view Context, int ErrorCode);

    std::uintptr_t Socket = static_cast<std::uintptr_t>(-1);
    FNetAddress LocalAddress;
    std::string LastError;
    bool bPlatformInitialized = false;
};
}
