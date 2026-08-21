#pragma once

#include "Pico/Net/NetPacket.h"

#include <deque>
#include <span>
#include <string>
#include <unordered_map>

namespace Pico
{
struct FNetConnectionConfig
{
    double HandshakeRetrySeconds = 0.25;
    double ReliableRetrySeconds = 0.15;
    double HeartbeatSeconds = 0.5;
    double HandshakeTimeoutSeconds = 5.0;
    double ConnectionTimeoutSeconds = 5.0;
    std::size_t MaxReliableQueue = 128;
    uint32 MaxReliableAttempts = 16;
    std::size_t MaxPacketsPerFlush = 32;
};

struct FNetOutboundPacket
{
    FNetAddress RemoteAddress;
    std::vector<uint8> Bytes;
};

class FNetConnection
{
public:
    explicit FNetConnection(FNetConnectionConfig InConfig = {});

    void StartClient(
        const FNetAddress& RemoteAddress,
        uint64 ClientNonce,
        double NowSeconds);
    void StartServer(
        const FNetAddress& RemoteAddress,
        FNetConnectionId ConnectionId,
        uint64 ClientNonce,
        double NowSeconds);
    void Close(std::string Reason);

    bool HandlePacket(const FDecodedNetPacket& Packet, double NowSeconds);
    void Tick(double NowSeconds);
    std::vector<FNetOutboundPacket> BuildOutgoingPackets(double NowSeconds);
    bool QueueReliable(
        std::span<const uint8> Payload,
        uint32* OutReliableId = nullptr);
    bool QueueUnreliable(std::span<const uint8> Payload);
    std::vector<std::vector<uint8>> ConsumeDeliveredReliableMessages();
    std::vector<std::vector<uint8>> ConsumeDeliveredUnreliableMessages();
    std::vector<uint32> ConsumeAcknowledgedReliableIds();

    ENetConnectionState GetState() const { return State; }
    FNetConnectionId GetConnectionId() const { return ConnectionId; }
    const FNetAddress& GetRemoteAddress() const { return RemoteAddress; }
    const FNetStatistics& GetStatistics() const { return Statistics; }
    std::size_t GetPendingReliableCount() const { return PendingReliable.size(); }
    const std::string& GetCloseReason() const { return CloseReason; }
    bool IsServerSide() const { return bServerSide; }

    static bool TryReadClientHello(
        const FDecodedNetPacket& Packet,
        uint64& OutClientNonce);

private:
    friend class FNetDriver;

    struct FPendingReliable
    {
        uint32 ReliableId = 0;
        std::vector<uint8> Payload;
        double LastSendSeconds = -1.0;
        uint32 Attempts = 0;
    };

    struct FSentPacket
    {
        uint32 Sequence = 0;
        uint32 ReliableId = 0;
        double SendSeconds = 0.0;
    };

    bool RegisterReceivedSequence(uint32 Sequence);
    void ProcessAcks(uint32 Ack, uint32 AckBits, double NowSeconds);
    FNetOutboundPacket BuildPacket(
        ENetPacketFlags Flags,
        std::span<const uint8> Payload,
        uint32 ReliableId,
        double NowSeconds);
    std::vector<uint8> BuildHandshakePayload(ENetHandshakeType Type) const;
    bool HandleHandshake(const FDecodedNetPacket& Packet, double NowSeconds);
    bool HandleReliable(const FDecodedNetPacket& Packet);
    bool IsPacketAcknowledged(
        uint32 Sequence,
        uint32 Ack,
        uint32 AckBits) const;

    FNetConnectionConfig Config;
    ENetConnectionState State = ENetConnectionState::Closed;
    FNetConnectionId ConnectionId;
    FNetAddress RemoteAddress;
    FNetStatistics Statistics;
    std::string CloseReason;
    uint64 ClientNonce = 0;
    uint32 NextOutgoingSequence = 1;
    uint32 HighestReceivedSequence = 0;
    uint32 ReceivedSequenceBits = 0;
    uint32 NextReliableId = 1;
    uint32 NextExpectedReliableId = 1;
    double StartSeconds = 0.0;
    double LastReceiveSeconds = 0.0;
    double LastSendSeconds = -1.0;
    double LastHandshakeSendSeconds = -1.0;
    bool bHasReceivedSequence = false;
    bool bAckPending = false;
    bool bServerSide = false;
    bool bClientAckPending = false;
    std::deque<FPendingReliable> PendingReliable;
    std::deque<std::vector<uint8>> PendingUnreliable;
    std::deque<FSentPacket> SentPackets;
    std::unordered_map<uint32, std::vector<uint8>> BufferedReliableMessages;
    std::vector<std::vector<uint8>> DeliveredReliableMessages;
    std::vector<std::vector<uint8>> DeliveredUnreliableMessages;
    std::vector<uint32> AcknowledgedReliableIds;
};
}
