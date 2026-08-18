#include "Pico/Net/LoopbackTransport.h"

#include <algorithm>

namespace Pico
{
void FLoopbackNetwork::AdvanceTime(double DeltaSeconds)
{
    CurrentTime += std::max(0.0, DeltaSeconds);
}

void FLoopbackNetwork::DropNextPacket()
{
    bDropNextPacket = true;
}

void FLoopbackNetwork::DuplicateNextPacket()
{
    bDuplicateNextPacket = true;
}

void FLoopbackNetwork::DelayNextPacket(double DelaySeconds)
{
    NextPacketDelay = std::max(0.0, DelaySeconds);
}

bool FLoopbackNetwork::Register(FNetAddress& Address)
{
    if (!Address.Host.empty() && Address.Port == 0)
    {
        do
        {
            Address.Port = NextEphemeralPort++;
            if (NextEphemeralPort == 0) NextEphemeralPort = 40000;
        }
        while (OpenAddresses.contains(Address.ToString()));
    }
    return Address.IsValid() && OpenAddresses.insert(Address.ToString()).second;
}

void FLoopbackNetwork::Unregister(const FNetAddress& Address)
{
    OpenAddresses.erase(Address.ToString());
}

bool FLoopbackNetwork::Send(
    const FNetAddress& From,
    const FNetAddress& To,
    std::span<const uint8> Bytes)
{
    if (!OpenAddresses.contains(From.ToString())
        || !OpenAddresses.contains(To.ToString())
        || Bytes.size() > MaxNetDatagramBytes)
    {
        return false;
    }
    if (bDropNextPacket)
    {
        bDropNextPacket = false;
        NextPacketDelay = 0.0;
        return true;
    }

    FQueuedPacket Packet;
    Packet.From = From;
    Packet.To = To;
    Packet.Bytes.assign(Bytes.begin(), Bytes.end());
    Packet.DeliveryTime = CurrentTime + NextPacketDelay;
    NextPacketDelay = 0.0;
    Packets.push_back(Packet);
    if (bDuplicateNextPacket)
    {
        Packets.push_back(Packet);
        bDuplicateNextPacket = false;
    }
    return true;
}

bool FLoopbackNetwork::Receive(
    const FNetAddress& LocalAddress,
    FNetAddress& OutRemoteAddress,
    std::vector<uint8>& OutBytes)
{
    const auto It = std::find_if(
        Packets.begin(), Packets.end(),
        [&](const FQueuedPacket& Packet)
        {
            return Packet.To == LocalAddress
                && Packet.DeliveryTime <= CurrentTime;
        });
    if (It == Packets.end()) return false;
    OutRemoteAddress = It->From;
    OutBytes = std::move(It->Bytes);
    Packets.erase(It);
    return true;
}

FLoopbackTransport::FLoopbackTransport(
    std::shared_ptr<FLoopbackNetwork> InNetwork)
    : Network(std::move(InNetwork))
{
}

FLoopbackTransport::~FLoopbackTransport()
{
    Close();
}

bool FLoopbackTransport::Open(const FNetAddress& InLocalAddress)
{
    Close();
    FNetAddress RequestedAddress = InLocalAddress;
    if (Network == nullptr || !Network->Register(RequestedAddress))
    {
        LastError = "loopback address is invalid or already open";
        return false;
    }
    LocalAddress = std::move(RequestedAddress);
    LastError.clear();
    bOpen = true;
    return true;
}

void FLoopbackTransport::Close()
{
    if (bOpen && Network != nullptr) Network->Unregister(LocalAddress);
    LocalAddress = {};
    bOpen = false;
}

bool FLoopbackTransport::IsOpen() const
{
    return bOpen;
}

bool FLoopbackTransport::SendTo(
    const FNetAddress& RemoteAddress,
    std::span<const uint8> Bytes)
{
    if (!bOpen || !Network->Send(LocalAddress, RemoteAddress, Bytes))
    {
        LastError = "loopback destination is not open";
        return false;
    }
    return true;
}

ENetReceiveResult FLoopbackTransport::ReceiveFrom(
    FNetAddress& OutRemoteAddress,
    std::vector<uint8>& OutBytes)
{
    if (!bOpen)
    {
        LastError = "loopback transport is closed";
        return ENetReceiveResult::Error;
    }
    return Network->Receive(LocalAddress, OutRemoteAddress, OutBytes)
        ? ENetReceiveResult::Packet : ENetReceiveResult::None;
}

const FNetAddress& FLoopbackTransport::GetLocalAddress() const
{
    return LocalAddress;
}

const std::string& FLoopbackTransport::GetLastError() const
{
    return LastError;
}
}
