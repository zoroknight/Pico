#pragma once

#include "Pico/Net/NetTransport.h"

#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Pico
{
class FLoopbackNetwork
{
public:
    void AdvanceTime(double DeltaSeconds);
    void DropNextPacket();
    void DuplicateNextPacket();
    void DelayNextPacket(double DelaySeconds);

private:
    struct FQueuedPacket
    {
        FNetAddress From;
        FNetAddress To;
        std::vector<uint8> Bytes;
        double DeliveryTime = 0.0;
    };

    bool Register(FNetAddress& Address);
    void Unregister(const FNetAddress& Address);
    bool Send(
        const FNetAddress& From,
        const FNetAddress& To,
        std::span<const uint8> Bytes);
    bool Receive(
        const FNetAddress& LocalAddress,
        FNetAddress& OutRemoteAddress,
        std::vector<uint8>& OutBytes);

    double CurrentTime = 0.0;
    bool bDropNextPacket = false;
    bool bDuplicateNextPacket = false;
    double NextPacketDelay = 0.0;
    std::unordered_set<std::string> OpenAddresses;
    std::deque<FQueuedPacket> Packets;
    uint16 NextEphemeralPort = 40000;

    friend class FLoopbackTransport;
};

class FLoopbackTransport final : public INetTransport
{
public:
    explicit FLoopbackTransport(std::shared_ptr<FLoopbackNetwork> InNetwork);
    ~FLoopbackTransport() override;

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
    std::shared_ptr<FLoopbackNetwork> Network;
    FNetAddress LocalAddress;
    std::string LastError;
    bool bOpen = false;
};
}
