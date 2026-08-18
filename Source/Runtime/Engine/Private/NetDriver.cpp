#include "Pico/Engine/NetDriver.h"

#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Log.h"
#include "Pico/Net/NetPacket.h"
#include "Pico/Net/UdpTransport.h"

#include <algorithm>
#include <chrono>

namespace Pico
{
namespace
{
const FNetAddress EmptyNetAddress;
constexpr std::size_t MaxPacketsPerDispatch = 256;
}

const char* ToString(ENetMode Mode)
{
    switch (Mode)
    {
    case ENetMode::Standalone: return "Standalone";
    case ENetMode::Server: return "Server";
    case ENetMode::Client: return "Client";
    }
    return "Unknown";
}

const char* ToString(ENetRole Role)
{
    switch (Role)
    {
    case ENetRole::None: return "None";
    case ENetRole::SimulatedProxy: return "SimulatedProxy";
    case ENetRole::AutonomousProxy: return "AutonomousProxy";
    case ENetRole::Authority: return "Authority";
    }
    return "Unknown";
}

bool FNetDriver::InitializeFromCommandLine()
{
    const bool bServer = FCommandLine::HasSwitch("server");
    const std::optional<std::string> Client = FCommandLine::GetValue("client");
    if (bServer && Client.has_value())
    {
        LastError = "-server and -client cannot be used together";
        return false;
    }

    const int Port = FCommandLine::GetInt("port").value_or(7777);
    if (Port <= 0 || Port > 65535)
    {
        LastError = "network port must be between 1 and 65535";
        return false;
    }
    if (bServer) return InitializeServer(static_cast<uint16>(Port));
    if (Client.has_value())
    {
        return InitializeClient(
            FNetAddress { *Client, static_cast<uint16>(Port) });
    }

    Shutdown();
    NetMode = ENetMode::Standalone;
    LastError.clear();
    bInitialized = true;
    LastError.clear();
    return true;
}

bool FNetDriver::InitializeServer(
    uint16 Port,
    std::unique_ptr<INetTransport> InTransport)
{
    Shutdown();
    NetMode = ENetMode::Server;
    Transport = InTransport != nullptr
        ? std::move(InTransport) : std::make_unique<FUdpTransport>();
    if (!Transport->Open(FNetAddress { "0.0.0.0", Port }))
    {
        LastError = Transport->GetLastError();
        Shutdown();
        return false;
    }
    bInitialized = true;
    LastError.clear();
    PICO_LOG(LogNet, Info, "NetDriver server listening on {}",
        Transport->GetLocalAddress().ToString());
    return true;
}

bool FNetDriver::InitializeClient(
    const FNetAddress& ServerAddress,
    std::unique_ptr<INetTransport> InTransport)
{
    Shutdown();
    if (!ServerAddress.IsValid())
    {
        LastError = "client server address is invalid";
        return false;
    }
    NetMode = ENetMode::Client;
    Transport = InTransport != nullptr
        ? std::move(InTransport) : std::make_unique<FUdpTransport>();
    if (!Transport->Open(FNetAddress { "0.0.0.0", 0 }))
    {
        LastError = Transport->GetLastError();
        Shutdown();
        return false;
    }

    auto Connection = std::make_unique<FNetConnection>();
    Connection->StartClient(ServerAddress, MakeClientNonce(), ElapsedSeconds);
    Connections.push_back(std::move(Connection));
    bInitialized = true;
    PICO_LOG(LogNet, Info, "NetDriver client {} connecting to {}",
        Transport->GetLocalAddress().ToString(), ServerAddress.ToString());
    return true;
}

void FNetDriver::Shutdown()
{
    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        if (Connection != nullptr) Connection->Close("NetDriver shutdown");
    }
    Connections.clear();
    if (Transport != nullptr) Transport->Close();
    Transport.reset();
    NetMode = ENetMode::Standalone;
    NextConnectionId.Value = 1;
    ElapsedSeconds = 0.0;
    InvalidPacketCount = 0;
    bInitialized = false;
}

void FNetDriver::TickDispatch(float DeltaSeconds)
{
    if (!bInitialized) return;
    ElapsedSeconds += std::max(0.0f, DeltaSeconds);
    if (Transport == nullptr) return;

    for (std::size_t Index = 0; Index < MaxPacketsPerDispatch; ++Index)
    {
        FNetAddress RemoteAddress;
        std::vector<uint8> Bytes;
        const ENetReceiveResult Result =
            Transport->ReceiveFrom(RemoteAddress, Bytes);
        if (Result == ENetReceiveResult::None) break;
        if (Result == ENetReceiveResult::Error)
        {
            LastError = Transport->GetLastError();
            break;
        }
        FDecodedNetPacket Packet;
        if (!DecodeNetPacket(Bytes, Packet))
        {
            ++InvalidPacketCount;
            continue;
        }
        DispatchPacket(RemoteAddress, Packet);
    }

    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        const ENetConnectionState PreviousState = Connection->GetState();
        Connection->Tick(ElapsedSeconds);
        if (PreviousState != ENetConnectionState::Closed
            && Connection->GetState() == ENetConnectionState::Closed)
        {
            PICO_LOG(LogNet, Info, "Connection {} to {} closed: {}",
                Connection->GetConnectionId().Value,
                Connection->GetRemoteAddress().ToString(),
                Connection->GetCloseReason());
        }
    }
    if (NetMode == ENetMode::Server)
    {
        std::erase_if(Connections,
            [](const std::unique_ptr<FNetConnection>& Connection)
            {
                return Connection->GetState() == ENetConnectionState::Closed;
            });
    }
}

void FNetDriver::TickFlush(float)
{
    if (!bInitialized || Transport == nullptr) return;
    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        for (const FNetOutboundPacket& Packet :
            Connection->BuildOutgoingPackets(ElapsedSeconds))
        {
            if (!Packet.Bytes.empty()
                && !Transport->SendTo(Packet.RemoteAddress, Packet.Bytes))
            {
                LastError = Transport->GetLastError();
            }
        }
    }
}

bool FNetDriver::QueueReliableToAll(std::span<const uint8> Payload)
{
    bool bQueuedAny = false;
    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        if (Connection->GetState() == ENetConnectionState::Open)
        {
            bQueuedAny = Connection->QueueReliable(Payload) || bQueuedAny;
        }
    }
    return bQueuedAny;
}

const FNetAddress& FNetDriver::GetLocalAddress() const
{
    return Transport != nullptr
        ? Transport->GetLocalAddress() : EmptyNetAddress;
}

std::vector<FNetConnectionSnapshot> FNetDriver::GetConnectionSnapshots() const
{
    std::vector<FNetConnectionSnapshot> Result;
    Result.reserve(Connections.size());
    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        FNetConnectionSnapshot Snapshot;
        Snapshot.ConnectionId = Connection->GetConnectionId();
        Snapshot.RemoteAddress = Connection->GetRemoteAddress();
        Snapshot.State = Connection->GetState();
        Snapshot.Statistics = Connection->GetStatistics();
        Snapshot.PendingReliableMessages =
            Connection->GetPendingReliableCount();
        Snapshot.CloseReason = Connection->GetCloseReason();
        Result.push_back(std::move(Snapshot));
    }
    return Result;
}

std::size_t FNetDriver::GetOpenConnectionCount() const
{
    return static_cast<std::size_t>(std::count_if(
        Connections.begin(), Connections.end(),
        [](const std::unique_ptr<FNetConnection>& Connection)
        {
            return Connection->GetState() == ENetConnectionState::Open;
        }));
}

FNetConnection* FNetDriver::FindConnection(
    FNetConnectionId ConnectionId,
    const FNetAddress& RemoteAddress) const
{
    const auto It = std::find_if(
        Connections.begin(), Connections.end(),
        [&](const std::unique_ptr<FNetConnection>& Connection)
        {
            return Connection->GetConnectionId() == ConnectionId
                && Connection->GetRemoteAddress() == RemoteAddress;
        });
    return It != Connections.end() ? It->get() : nullptr;
}

FNetConnection* FNetDriver::FindConnectionByAddress(
    const FNetAddress& RemoteAddress) const
{
    const auto It = std::find_if(
        Connections.begin(), Connections.end(),
        [&](const std::unique_ptr<FNetConnection>& Connection)
        {
            return Connection->GetRemoteAddress() == RemoteAddress;
        });
    return It != Connections.end() ? It->get() : nullptr;
}

void FNetDriver::DispatchPacket(
    const FNetAddress& RemoteAddress,
    const FDecodedNetPacket& Packet)
{
    FNetConnection* Connection = nullptr;
    if (NetMode == ENetMode::Server)
    {
        Connection = Packet.Header.ConnectionId.IsValid()
            ? FindConnection(Packet.Header.ConnectionId, RemoteAddress)
            : FindConnectionByAddress(RemoteAddress);
        if (Connection == nullptr && !Packet.Header.ConnectionId.IsValid())
        {
            uint64 ClientNonce = 0;
            if (FNetConnection::TryReadClientHello(Packet, ClientNonce))
            {
                auto NewConnection = std::make_unique<FNetConnection>();
                FNetConnectionId AssignedId = NextConnectionId;
                if (++NextConnectionId.Value == 0) ++NextConnectionId.Value;
                NewConnection->StartServer(
                    RemoteAddress, AssignedId, ClientNonce, ElapsedSeconds);
                Connection = NewConnection.get();
                Connections.push_back(std::move(NewConnection));
                PICO_LOG(LogNet, Info,
                    "Accepted ClientHello from {} as connection {}",
                    RemoteAddress.ToString(), AssignedId.Value);
            }
        }
    }
    else if (NetMode == ENetMode::Client && !Connections.empty())
    {
        Connection = Connections.front().get();
        if (Connection->GetState() == ENetConnectionState::Handshaking)
        {
            if (RemoteAddress.Port != Connection->GetRemoteAddress().Port)
            {
                Connection = nullptr;
            }
            else
            {
                Connection->RemoteAddress = RemoteAddress;
            }
        }
        else if (RemoteAddress != Connection->GetRemoteAddress())
        {
            Connection = nullptr;
        }
    }

    const ENetConnectionState PreviousState = Connection != nullptr
        ? Connection->GetState() : ENetConnectionState::Closed;
    if (Connection == nullptr
        || !Connection->HandlePacket(Packet, ElapsedSeconds))
    {
        ++InvalidPacketCount;
        return;
    }
    if (PreviousState != ENetConnectionState::Open
        && Connection->GetState() == ENetConnectionState::Open)
    {
        PICO_LOG(LogNet, Info, "Connection {} to {} is open",
            Connection->GetConnectionId().Value,
            Connection->GetRemoteAddress().ToString());
    }
}

uint64 FNetDriver::MakeClientNonce() const
{
    const uint64 Time = static_cast<uint64>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return Time ^ (static_cast<uint64>(GetLocalAddress().Port) << 32)
        ^ 0x9e3779b97f4a7c15ull;
}
}
