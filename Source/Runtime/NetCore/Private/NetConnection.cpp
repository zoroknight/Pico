#include "Pico/Net/NetConnection.h"

#include <algorithm>

namespace Pico
{
FNetConnection::FNetConnection(FNetConnectionConfig InConfig)
    : Config(InConfig)
{
}

void FNetConnection::StartClient(
    const FNetAddress& InRemoteAddress,
    uint64 InClientNonce,
    double NowSeconds)
{
    *this = FNetConnection(Config);
    State = ENetConnectionState::Handshaking;
    RemoteAddress = InRemoteAddress;
    ClientNonce = InClientNonce;
    StartSeconds = NowSeconds;
    LastReceiveSeconds = NowSeconds;
}

void FNetConnection::StartServer(
    const FNetAddress& InRemoteAddress,
    FNetConnectionId InConnectionId,
    uint64 InClientNonce,
    double NowSeconds)
{
    *this = FNetConnection(Config);
    State = ENetConnectionState::Handshaking;
    ConnectionId = InConnectionId;
    RemoteAddress = InRemoteAddress;
    ClientNonce = InClientNonce;
    StartSeconds = NowSeconds;
    LastReceiveSeconds = NowSeconds;
    bServerSide = true;
}

void FNetConnection::Close(std::string Reason)
{
    State = ENetConnectionState::Closed;
    CloseReason = std::move(Reason);
    PendingReliable.clear();
    SentPackets.clear();
    BufferedReliableMessages.clear();
    DeliveredReliableMessages.clear();
}

bool FNetConnection::HandlePacket(
    const FDecodedNetPacket& Packet,
    double NowSeconds)
{
    if (State == ENetConnectionState::Closed) return false;
    const bool bClientWelcome = !bServerSide
        && State == ENetConnectionState::Handshaking
        && HasAnyFlags(Packet.Header.Flags, ENetPacketFlags::Handshake);
    const bool bServerHello = bServerSide
        && State == ENetConnectionState::Handshaking
        && !Packet.Header.ConnectionId.IsValid();
    if (!bClientWelcome && !bServerHello
        && Packet.Header.ConnectionId != ConnectionId)
    {
        ++Statistics.PacketsDropped;
        return false;
    }

    ++Statistics.PacketsReceived;
    LastReceiveSeconds = NowSeconds;
    ProcessAcks(Packet.Header.Ack, Packet.Header.AckBits, NowSeconds);
    if (!RegisterReceivedSequence(Packet.Header.Sequence)) return true;

    if (HasAnyFlags(Packet.Header.Flags, ENetPacketFlags::Handshake))
    {
        bAckPending = true;
        return HandleHandshake(Packet, NowSeconds);
    }
    if (State != ENetConnectionState::Open) return false;
    if (HasAnyFlags(Packet.Header.Flags, ENetPacketFlags::Reliable))
    {
        bAckPending = true;
        return HandleReliable(Packet);
    }
    if (HasAnyFlags(Packet.Header.Flags, ENetPacketFlags::Heartbeat))
    {
        bAckPending = true;
    }
    return true;
}

void FNetConnection::Tick(double NowSeconds)
{
    if (State == ENetConnectionState::Handshaking
        && NowSeconds - StartSeconds > Config.HandshakeTimeoutSeconds)
    {
        Close("handshake timed out");
        return;
    }
    if (State == ENetConnectionState::Open
        && NowSeconds - LastReceiveSeconds > Config.ConnectionTimeoutSeconds)
    {
        Close("connection timed out");
    }
}

std::vector<FNetOutboundPacket> FNetConnection::BuildOutgoingPackets(
    double NowSeconds)
{
    std::vector<FNetOutboundPacket> Result;
    if (State == ENetConnectionState::Closed) return Result;

    if (State == ENetConnectionState::Handshaking
        && (LastHandshakeSendSeconds < 0.0
            || NowSeconds - LastHandshakeSendSeconds
                >= Config.HandshakeRetrySeconds))
    {
        const ENetHandshakeType Type = bServerSide
            ? ENetHandshakeType::ServerWelcome
            : ENetHandshakeType::ClientHello;
        const std::vector<uint8> Payload = BuildHandshakePayload(Type);
        Result.push_back(BuildPacket(
            ENetPacketFlags::Handshake, Payload, 0, NowSeconds));
        LastHandshakeSendSeconds = NowSeconds;
    }

    if (State == ENetConnectionState::Open && bClientAckPending)
    {
        const std::vector<uint8> Payload =
            BuildHandshakePayload(ENetHandshakeType::ClientAck);
        Result.push_back(BuildPacket(
            ENetPacketFlags::Handshake, Payload, 0, NowSeconds));
        bClientAckPending = false;
    }

    if (State == ENetConnectionState::Open)
    {
        for (FPendingReliable& Reliable : PendingReliable)
        {
            if (Result.size() >= Config.MaxPacketsPerFlush) break;
            if (Reliable.LastSendSeconds >= 0.0
                && NowSeconds - Reliable.LastSendSeconds
                    < Config.ReliableRetrySeconds)
            {
                continue;
            }
            if (Reliable.Attempts >= Config.MaxReliableAttempts)
            {
                Close("reliable message exceeded retry limit");
                return {};
            }

            FNetByteWriter Writer;
            Writer.WriteUInt32(Reliable.ReliableId);
            Writer.WriteBytes(Reliable.Payload);
            Result.push_back(BuildPacket(
                ENetPacketFlags::Reliable,
                Writer.GetBytes(),
                Reliable.ReliableId,
                NowSeconds));
            if (Reliable.Attempts == 0) ++Statistics.ReliableMessagesSent;
            else ++Statistics.ReliableMessagesResent;
            Reliable.LastSendSeconds = NowSeconds;
            ++Reliable.Attempts;
        }

        if (Result.empty()
            && (LastSendSeconds < 0.0
                || NowSeconds - LastSendSeconds >= Config.HeartbeatSeconds))
        {
            Result.push_back(BuildPacket(
                ENetPacketFlags::Heartbeat, {}, 0, NowSeconds));
        }
        else if (Result.empty() && bAckPending)
        {
            Result.push_back(BuildPacket(
                ENetPacketFlags::None, {}, 0, NowSeconds));
        }
    }
    return Result;
}

bool FNetConnection::QueueReliable(std::span<const uint8> Payload)
{
    constexpr std::size_t ReliableHeaderBytes = 4;
    constexpr std::size_t PacketHeaderBytes = 26;
    if (State != ENetConnectionState::Open || Payload.empty()
        || PendingReliable.size() >= Config.MaxReliableQueue
        || Payload.size() + ReliableHeaderBytes + PacketHeaderBytes
            > MaxNetDatagramBytes)
    {
        return false;
    }
    FPendingReliable Reliable;
    Reliable.ReliableId = NextReliableId++;
    if (Reliable.ReliableId == 0) Reliable.ReliableId = NextReliableId++;
    Reliable.Payload.assign(Payload.begin(), Payload.end());
    PendingReliable.push_back(std::move(Reliable));
    return true;
}

std::vector<std::vector<uint8>>
FNetConnection::ConsumeDeliveredReliableMessages()
{
    std::vector<std::vector<uint8>> Result =
        std::move(DeliveredReliableMessages);
    DeliveredReliableMessages.clear();
    return Result;
}

bool FNetConnection::TryReadClientHello(
    const FDecodedNetPacket& Packet,
    uint64& OutClientNonce)
{
    if (!HasAnyFlags(Packet.Header.Flags, ENetPacketFlags::Handshake)
        || Packet.Header.ConnectionId.IsValid())
    {
        return false;
    }
    FNetByteReader Reader(Packet.Payload);
    uint8 Type = 0;
    return Reader.ReadUInt8(Type)
        && Type == static_cast<uint8>(ENetHandshakeType::ClientHello)
        && Reader.ReadUInt64(OutClientNonce)
        && Reader.GetRemainingBytes() == 0;
}

bool FNetConnection::RegisterReceivedSequence(uint32 Sequence)
{
    if (!bHasReceivedSequence)
    {
        HighestReceivedSequence = Sequence;
        ReceivedSequenceBits = 0;
        bHasReceivedSequence = true;
        return true;
    }
    if (Sequence == HighestReceivedSequence)
    {
        ++Statistics.DuplicatePackets;
        return false;
    }
    if (IsNetSequenceNewer(Sequence, HighestReceivedSequence))
    {
        const uint32 Difference = Sequence - HighestReceivedSequence;
        if (Difference > 32)
        {
            ReceivedSequenceBits = 0;
        }
        else
        {
            ReceivedSequenceBits <<= Difference;
            ReceivedSequenceBits |= 1u << (Difference - 1);
        }
        HighestReceivedSequence = Sequence;
        return true;
    }

    const uint32 Distance = HighestReceivedSequence - Sequence;
    if (Distance == 0 || Distance > 32)
    {
        ++Statistics.PacketsDropped;
        return false;
    }
    const uint32 Bit = 1u << (Distance - 1);
    if ((ReceivedSequenceBits & Bit) != 0)
    {
        ++Statistics.DuplicatePackets;
        return false;
    }
    ReceivedSequenceBits |= Bit;
    ++Statistics.OutOfOrderPackets;
    return true;
}

void FNetConnection::ProcessAcks(
    uint32 Ack,
    uint32 AckBits,
    double NowSeconds)
{
    for (auto It = SentPackets.begin(); It != SentPackets.end();)
    {
        if (!IsPacketAcknowledged(It->Sequence, Ack, AckBits))
        {
            ++It;
            continue;
        }
        const double Sample = std::max(0.0, NowSeconds - It->SendSeconds);
        Statistics.SmoothedRoundTripSeconds =
            Statistics.SmoothedRoundTripSeconds <= 0.0
            ? Sample
            : Statistics.SmoothedRoundTripSeconds * 0.875 + Sample * 0.125;
        if (It->ReliableId != 0)
        {
            std::erase_if(
                PendingReliable,
                [&](const FPendingReliable& Reliable)
                {
                    return Reliable.ReliableId == It->ReliableId;
                });
        }
        It = SentPackets.erase(It);
    }
}

FNetOutboundPacket FNetConnection::BuildPacket(
    ENetPacketFlags Flags,
    std::span<const uint8> Payload,
    uint32 ReliableId,
    double NowSeconds)
{
    FNetPacketHeader Header;
    Header.Flags = Flags;
    Header.ConnectionId = ConnectionId;
    Header.Sequence = NextOutgoingSequence++;
    Header.Ack = bHasReceivedSequence ? HighestReceivedSequence : 0;
    Header.AckBits = bHasReceivedSequence ? ReceivedSequenceBits : 0;

    FNetOutboundPacket Result;
    Result.RemoteAddress = RemoteAddress;
    EncodeNetPacket(Header, Payload, Result.Bytes);
    SentPackets.push_back({ Header.Sequence, ReliableId, NowSeconds });
    while (SentPackets.size() > 256) SentPackets.pop_front();
    ++Statistics.PacketsSent;
    LastSendSeconds = NowSeconds;
    bAckPending = false;
    return Result;
}

std::vector<uint8> FNetConnection::BuildHandshakePayload(
    ENetHandshakeType Type) const
{
    FNetByteWriter Writer;
    Writer.WriteUInt8(static_cast<uint8>(Type));
    Writer.WriteUInt64(ClientNonce);
    return Writer.GetBytes();
}

bool FNetConnection::HandleHandshake(
    const FDecodedNetPacket& Packet,
    double NowSeconds)
{
    FNetByteReader Reader(Packet.Payload);
    uint8 RawType = 0;
    uint64 Nonce = 0;
    if (!Reader.ReadUInt8(RawType) || !Reader.ReadUInt64(Nonce)
        || Reader.GetRemainingBytes() != 0 || Nonce != ClientNonce)
    {
        ++Statistics.PacketsDropped;
        return false;
    }

    const ENetHandshakeType Type = static_cast<ENetHandshakeType>(RawType);
    if (!bServerSide && Type == ENetHandshakeType::ServerWelcome
        && Packet.Header.ConnectionId.IsValid())
    {
        ConnectionId = Packet.Header.ConnectionId;
        State = ENetConnectionState::Open;
        bClientAckPending = true;
        LastReceiveSeconds = NowSeconds;
        return true;
    }
    if (bServerSide && Type == ENetHandshakeType::ClientAck
        && Packet.Header.ConnectionId == ConnectionId)
    {
        State = ENetConnectionState::Open;
        LastReceiveSeconds = NowSeconds;
        return true;
    }
    if (bServerSide && Type == ENetHandshakeType::ClientHello)
    {
        LastHandshakeSendSeconds = -1.0;
        return true;
    }
    return false;
}

bool FNetConnection::HandleReliable(const FDecodedNetPacket& Packet)
{
    FNetByteReader Reader(Packet.Payload);
    uint32 ReliableId = 0;
    std::vector<uint8> Payload;
    if (!Reader.ReadUInt32(ReliableId) || ReliableId == 0
        || !Reader.ReadBytes(Reader.GetRemainingBytes(), Payload))
    {
        ++Statistics.PacketsDropped;
        return false;
    }
    if (ReliableId != NextExpectedReliableId
        && !IsNetSequenceNewer(ReliableId, NextExpectedReliableId))
    {
        return true;
    }
    const bool bInserted = BufferedReliableMessages.emplace(
        ReliableId, std::move(Payload)).second;
    if (!bInserted) return true;
    if (BufferedReliableMessages.size() > Config.MaxReliableQueue)
    {
        ++Statistics.PacketsDropped;
        Close("reliable receive buffer exceeded capacity");
        return false;
    }

    for (;;)
    {
        const auto It = BufferedReliableMessages.find(NextExpectedReliableId);
        if (It == BufferedReliableMessages.end()) break;
        DeliveredReliableMessages.push_back(std::move(It->second));
        BufferedReliableMessages.erase(It);
        ++NextExpectedReliableId;
        if (NextExpectedReliableId == 0) ++NextExpectedReliableId;
        ++Statistics.ReliableMessagesDelivered;
    }
    return true;
}

bool FNetConnection::IsPacketAcknowledged(
    uint32 Sequence,
    uint32 Ack,
    uint32 AckBits) const
{
    if (Ack == 0) return false;
    if (Sequence == Ack) return true;
    if (IsNetSequenceNewer(Sequence, Ack)) return false;
    const uint32 Distance = Ack - Sequence;
    return Distance >= 1 && Distance <= 32
        && (AckBits & (1u << (Distance - 1))) != 0;
}
}
