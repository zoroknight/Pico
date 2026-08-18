#include "TestRunner.h"

#include "Pico/Net/LoopbackTransport.h"
#include "Pico/Net/NetConnection.h"
#include "Pico/Net/NetPacket.h"
#include "Pico/Net/UdpTransport.h"

#include <chrono>
#include <limits>
#include <memory>
#include <thread>

namespace
{
void TestPacketCodec(FTestRunner& Runner)
{
    Pico::FNetPacketHeader Header;
    Header.Flags = Pico::ENetPacketFlags::Reliable;
    Header.ConnectionId.Value = 42;
    Header.Sequence = 7;
    Header.Ack = 5;
    Header.AckBits = 0x15;
    const std::vector<Pico::uint8> Payload { 1, 2, 3, 4 };
    std::vector<Pico::uint8> Bytes;
    Pico::FDecodedNetPacket Decoded;
    Runner.Expect(
        Pico::EncodeNetPacket(Header, Payload, Bytes)
            && Pico::DecodeNetPacket(Bytes, Decoded)
            && Decoded.Header.ConnectionId.Value == 42
            && Decoded.Header.Sequence == 7
            && Decoded.Header.Ack == 5
            && Decoded.Header.AckBits == 0x15
            && Decoded.Payload == Payload,
        "Network packets round trip through an explicit wire format");

    std::vector<Pico::uint8> Truncated = Bytes;
    Truncated.pop_back();
    Runner.Expect(
        !Pico::DecodeNetPacket(Truncated, Decoded),
        "Truncated network packets are rejected");
    Bytes[5] = 2;
    Runner.Expect(
        !Pico::DecodeNetPacket(Bytes, Decoded),
        "Unknown network protocol versions are rejected");
    Runner.Expect(
        Pico::IsNetSequenceNewer(1, std::numeric_limits<Pico::uint32>::max())
            && !Pico::IsNetSequenceNewer(
                std::numeric_limits<Pico::uint32>::max(), 1),
        "Packet sequence comparison handles uint32 wraparound");
}

void TestLoopbackTransport(FTestRunner& Runner)
{
    auto Network = std::make_shared<Pico::FLoopbackNetwork>();
    Pico::FLoopbackTransport A(Network);
    Pico::FLoopbackTransport B(Network);
    const Pico::FNetAddress AddressA { "A", 1001 };
    const Pico::FNetAddress AddressB { "B", 1002 };
    Runner.Expect(
        A.Open(AddressA) && B.Open(AddressB),
        "Loopback endpoints open with unique addresses");

    const std::vector<Pico::uint8> Payload { 9, 8, 7 };
    Network->DropNextPacket();
    Runner.Expect(A.SendTo(AddressB, Payload), "Loopback can drop a selected packet");
    Pico::FNetAddress From;
    std::vector<Pico::uint8> Received;
    Runner.Expect(
        B.ReceiveFrom(From, Received) == Pico::ENetReceiveResult::None,
        "A selected loopback packet is deterministically dropped");

    Network->DuplicateNextPacket();
    A.SendTo(AddressB, Payload);
    const bool bFirst = B.ReceiveFrom(From, Received) == Pico::ENetReceiveResult::Packet;
    const bool bSecond = B.ReceiveFrom(From, Received) == Pico::ENetReceiveResult::Packet;
    Runner.Expect(bFirst && bSecond, "Loopback can duplicate a selected packet");

    Network->DelayNextPacket(0.25);
    A.SendTo(AddressB, Payload);
    Runner.Expect(
        B.ReceiveFrom(From, Received) == Pico::ENetReceiveResult::None,
        "Delayed loopback packets wait for simulated time");
    Network->AdvanceTime(0.25);
    Runner.Expect(
        B.ReceiveFrom(From, Received) == Pico::ENetReceiveResult::Packet,
        "Delayed loopback packets arrive after simulated time advances");
}

void SendPackets(
    Pico::FNetConnection& Connection,
    Pico::INetTransport& Transport,
    double NowSeconds)
{
    for (const Pico::FNetOutboundPacket& Packet :
        Connection.BuildOutgoingPackets(NowSeconds))
    {
        Transport.SendTo(Packet.RemoteAddress, Packet.Bytes);
    }
}

void ReceivePackets(
    Pico::INetTransport& Transport,
    Pico::FNetConnection& Connection,
    double NowSeconds)
{
    for (;;)
    {
        Pico::FNetAddress From;
        std::vector<Pico::uint8> Bytes;
        if (Transport.ReceiveFrom(From, Bytes) != Pico::ENetReceiveResult::Packet)
        {
            break;
        }
        Pico::FDecodedNetPacket Packet;
        if (Pico::DecodeNetPacket(Bytes, Packet))
        {
            Connection.HandlePacket(Packet, NowSeconds);
        }
    }
}

void TestConnectionHandshakeAndReliability(FTestRunner& Runner)
{
    auto Network = std::make_shared<Pico::FLoopbackNetwork>();
    Pico::FLoopbackTransport ClientTransport(Network);
    Pico::FLoopbackTransport ServerTransport(Network);
    const Pico::FNetAddress ClientAddress { "Client", 2001 };
    const Pico::FNetAddress ServerAddress { "Server", 2002 };
    ClientTransport.Open(ClientAddress);
    ServerTransport.Open(ServerAddress);

    Pico::FNetConnection Client;
    Pico::FNetConnection Server;
    Client.StartClient(ServerAddress, 0x12345678u, 0.0);
    double Now = 0.0;
    bool bServerStarted = false;
    for (int Step = 0; Step < 40
        && (Client.GetState() != Pico::ENetConnectionState::Open
            || Server.GetState() != Pico::ENetConnectionState::Open); ++Step)
    {
        Now += 0.05;
        Network->AdvanceTime(0.05);
        SendPackets(Client, ClientTransport, Now);

        for (;;)
        {
            Pico::FNetAddress From;
            std::vector<Pico::uint8> Bytes;
            if (ServerTransport.ReceiveFrom(From, Bytes)
                != Pico::ENetReceiveResult::Packet) break;
            Pico::FDecodedNetPacket Packet;
            if (!Pico::DecodeNetPacket(Bytes, Packet)) continue;
            if (!bServerStarted)
            {
                Pico::uint64 Nonce = 0;
                if (Pico::FNetConnection::TryReadClientHello(Packet, Nonce))
                {
                    Server.StartServer(From, { 1 }, Nonce, Now);
                    bServerStarted = true;
                }
            }
            if (bServerStarted) Server.HandlePacket(Packet, Now);
        }
        if (bServerStarted) SendPackets(Server, ServerTransport, Now);
        ReceivePackets(ClientTransport, Client, Now);
        Client.Tick(Now);
        if (bServerStarted) Server.Tick(Now);
    }
    Runner.Expect(
        Client.GetState() == Pico::ENetConnectionState::Open
            && Server.GetState() == Pico::ENetConnectionState::Open
            && Client.GetConnectionId().Value == 1,
        "Client and server complete the three-step handshake");

    const std::vector<Pico::uint8> ReliablePayload { 4, 5, 6 };
    Runner.Expect(
        Client.QueueReliable(ReliablePayload),
        "Open connections accept bounded reliable messages");
    Network->DropNextPacket();
    SendPackets(Client, ClientTransport, Now);
    Network->DuplicateNextPacket();
    for (int Step = 0; Step < 40 && Client.GetPendingReliableCount() != 0; ++Step)
    {
        Now += 0.05;
        Network->AdvanceTime(0.05);
        SendPackets(Client, ClientTransport, Now);
        ReceivePackets(ServerTransport, Server, Now);
        SendPackets(Server, ServerTransport, Now);
        ReceivePackets(ClientTransport, Client, Now);
        Client.Tick(Now);
        Server.Tick(Now);
    }
    const std::vector<std::vector<Pico::uint8>> Delivered =
        Server.ConsumeDeliveredReliableMessages();
    Runner.Expect(
        Delivered.size() == 1
            && Delivered.front() == ReliablePayload
            && Client.GetPendingReliableCount() == 0
            && Client.GetStatistics().ReliableMessagesResent >= 1,
        "Reliable messages survive loss and duplication with exactly-once delivery");

    const std::vector<Pico::uint8> FirstOrdered { 10 };
    const std::vector<Pico::uint8> SecondOrdered { 20 };
    Client.QueueReliable(FirstOrdered);
    Client.QueueReliable(SecondOrdered);
    Network->DelayNextPacket(0.2);
    SendPackets(Client, ClientTransport, Now);
    ReceivePackets(ServerTransport, Server, Now);
    Runner.Expect(
        Server.ConsumeDeliveredReliableMessages().empty(),
        "An out-of-order reliable message waits for the missing predecessor");
    Now += 0.2;
    Network->AdvanceTime(0.2);
    ReceivePackets(ServerTransport, Server, Now);
    const std::vector<std::vector<Pico::uint8>> Ordered =
        Server.ConsumeDeliveredReliableMessages();
    Runner.Expect(
        Ordered.size() == 2
            && Ordered[0] == FirstOrdered
            && Ordered[1] == SecondOrdered,
        "Reliable messages are delivered exactly once in ReliableId order");
    SendPackets(Server, ServerTransport, Now);
    ReceivePackets(ClientTransport, Client, Now);

    bool bFilledReliableQueue = true;
    for (std::size_t Index = 0; Index < 128; ++Index)
    {
        bFilledReliableQueue =
            Client.QueueReliable(std::span<const Pico::uint8>(ReliablePayload))
            && bFilledReliableQueue;
    }
    Runner.Expect(
        bFilledReliableQueue && !Client.QueueReliable(ReliablePayload),
        "Reliable queues enforce a hard capacity limit");
    Client.Close("queue limit test complete");

    Pico::FNetConnectionConfig SmallQueueConfig;
    SmallQueueConfig.MaxReliableQueue = 1;
    Pico::FNetConnection Limited(SmallQueueConfig);
    Limited.StartClient(ServerAddress, 7, 0.0);
    Runner.Expect(
        !Limited.QueueReliable(ReliablePayload),
        "Reliable messages cannot be queued before a connection opens");

    Pico::FNetConnection Timeout;
    Timeout.StartClient(ServerAddress, 8, 0.0);
    Timeout.Tick(6.0);
    Runner.Expect(
        Timeout.GetState() == Pico::ENetConnectionState::Closed
            && !Timeout.GetCloseReason().empty(),
        "Handshaking connections close after a bounded timeout");
}

void TestUdpTransport(FTestRunner& Runner)
{
    Pico::FUdpTransport Server;
    Pico::FUdpTransport Client;
    const bool bOpened = Server.Open({ "127.0.0.1", 0 })
        && Client.Open({ "127.0.0.1", 0 });
    Runner.Expect(bOpened, "UDP transports bind non-blocking localhost sockets");
    if (!bOpened) return;

    const std::vector<Pico::uint8> Payload { 1, 3, 5, 7 };
    const Pico::FNetAddress ServerAddress {
        "127.0.0.1", Server.GetLocalAddress().Port };
    bool bReceived = Client.SendTo(ServerAddress, Payload);
    Pico::FNetAddress From;
    std::vector<Pico::uint8> Received;
    for (int Attempt = 0; bReceived && Attempt < 100; ++Attempt)
    {
        const Pico::ENetReceiveResult Result =
            Server.ReceiveFrom(From, Received);
        if (Result == Pico::ENetReceiveResult::Packet) break;
        if (Result == Pico::ENetReceiveResult::Error)
        {
            bReceived = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Runner.Expect(
        bReceived && Received == Payload && From.Port == Client.GetLocalAddress().Port,
        "UDP transports exchange a datagram through an ephemeral localhost port");

    Pico::FUdpTransport ClosedEndpoint;
    const bool bOpenedClosedEndpoint = ClosedEndpoint.Open({ "127.0.0.1", 0 });
    const Pico::FNetAddress ClosedAddress {
        "127.0.0.1", ClosedEndpoint.GetLocalAddress().Port };
    ClosedEndpoint.Close();
    bool bSawReceiveError = !bOpenedClosedEndpoint
        || !Client.SendTo(ClosedAddress, Payload);
    for (int Attempt = 0; !bSawReceiveError && Attempt < 50; ++Attempt)
    {
        if (Client.ReceiveFrom(From, Received) == Pico::ENetReceiveResult::Error)
            bSawReceiveError = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Runner.Expect(
        !bSawReceiveError,
        "A temporarily unavailable UDP endpoint does not poison recvfrom");
}
}

int main()
{
    FTestRunner Runner;
    TestPacketCodec(Runner);
    TestLoopbackTransport(Runner);
    TestConnectionHandshakeAndReliability(Runner);
    TestUdpTransport(Runner);
    return Runner.Finish();
}
