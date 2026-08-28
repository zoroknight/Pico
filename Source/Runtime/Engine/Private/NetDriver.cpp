#include "Pico/Engine/NetDriver.h"

#include "Pico/Core/CommandLine.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Profiler.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/Character.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Function.h"
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
constexpr std::size_t MaxDelayedPackets = 4096;
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
    FNetworkSimulationSettings Simulation;
    Simulation.LatencyMs = FCommandLine::GetInt("netlatency").value_or(0);
    Simulation.JitterMs = FCommandLine::GetInt("netjitter").value_or(0);
    Simulation.PacketLossPercent =
        FCommandLine::GetInt("netloss").value_or(0);
    if (bServer)
    {
        const bool bResult = InitializeServer(static_cast<uint16>(Port));
        if (bResult) SetNetworkSimulationSettings(Simulation);
        return bResult;
    }
    if (Client.has_value())
    {
        const bool bResult = InitializeClient(
            FNetAddress { *Client, static_cast<uint16>(Port) });
        if (bResult) SetNetworkSimulationSettings(Simulation);
        return bResult;
    }

    Shutdown();
    NetMode = ENetMode::Standalone;
    LastError.clear();
    bInitialized = true;
    SetNetworkSimulationSettings(Simulation);
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
    if (World != nullptr && World->GetNetDriver() == this)
        World->SetNetDriver(nullptr);
    World = nullptr;
    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        if (Connection != nullptr) Connection->Close("NetDriver shutdown");
    }
    Connections.clear();
    ReplicationSystem.Reset();
    if (Transport != nullptr) Transport->Close();
    Transport.reset();
    NetMode = ENetMode::Standalone;
    NextConnectionId.Value = 1;
    ElapsedSeconds = 0.0;
    InvalidPacketCount = 0;
    OpenedConnections.clear();
    ClosedConnections.clear();
    DelayedPackets.clear();
    NetworkSimulation = {};
    SimulatedDroppedPacketCount = 0;
    SimulationRandomState = 0x5049434fu;
    bInitialized = false;
}

void FNetDriver::TickDispatch(float DeltaSeconds)
{
    PICO_PROFILE_SCOPE("Net.Receive");
    if (!bInitialized) return;
    ReplicationSystem.BeginNetworkFrame();
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
            ReplicationSystem.HandleConnectionClosed(
                Connection->GetConnectionId());
            ClosedConnections.push_back(Connection->GetConnectionId());
            PICO_LOG(LogNet, Info, "Connection {} to {} closed: {}",
                Connection->GetConnectionId().Value,
                Connection->GetRemoteAddress().ToString(),
                Connection->GetCloseReason());
        }
        for (const std::vector<uint8>& Message :
            Connection->ConsumeDeliveredReliableMessages())
        {
            ReplicationSystem.HandleReliableMessage(
                Connection->GetConnectionId(), Message);
        }
        for (const std::vector<uint8>& Message :
            Connection->ConsumeDeliveredUnreliableMessages())
        {
            ReplicationSystem.HandleUnreliableMessage(
                Connection->GetConnectionId(), Message);
        }
        for (const uint32 ReliableId :
            Connection->ConsumeAcknowledgedReliableIds())
        {
            ReplicationSystem.HandleReliableAcknowledged(
                Connection->GetConnectionId(), ReliableId);
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
    PICO_PROFILE_SCOPE("Net.Replication");
    if (!bInitialized || Transport == nullptr) return;
    FlushDelayedPackets();
    for (const std::unique_ptr<FNetConnection>& Connection : Connections)
    {
        if (NetMode == ENetMode::Server
            && Connection->GetState() == ENetConnectionState::Open)
        {
            ReplicationSystem.ReplicateServerConnection(
                Connection->GetConnectionId(),
                [&Connection](std::span<const uint8> Payload,
                    uint32* OutReliableId)
                {
                    return Connection->QueueReliable(
                        Payload, OutReliableId);
                },
                [&Connection](std::span<const uint8> Payload)
                {
                    return Connection->QueueUnreliable(Payload);
                });
        }
        for (const FNetOutboundPacket& Packet :
            Connection->BuildOutgoingPackets(ElapsedSeconds))
        {
            if (!Packet.Bytes.empty())
                SendOrDelayPacket(Packet.RemoteAddress, Packet.Bytes);
        }
    }
    FlushDelayedPackets();
}

void FNetDriver::SetWorld(PWorld* InWorld)
{
    if (this->World != nullptr && this->World->GetNetDriver() == this)
        this->World->SetNetDriver(nullptr);
    this->World = InWorld;
    if (this->World != nullptr) this->World->SetNetDriver(this);
    ReplicationSystem.SetWorld(this->World);
}

std::vector<FActorChannelSnapshot>
FNetDriver::GetActorChannelSnapshots() const
{
    return ReplicationSystem.GetChannelSnapshots();
}

FReplicationStatistics FNetDriver::GetReplicationStatistics() const
{
    return ReplicationSystem.GetStatistics();
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
        OpenedConnections.push_back(Connection->GetConnectionId());
        PICO_LOG(LogNet, Info, "Connection {} to {} is open",
            Connection->GetConnectionId().Value,
            Connection->GetRemoteAddress().ToString());
    }
}

std::vector<FNetConnectionId> FNetDriver::ConsumeOpenedConnections()
{
    std::vector<FNetConnectionId> Result = std::move(OpenedConnections);
    OpenedConnections.clear();
    return Result;
}

std::vector<FNetConnectionId> FNetDriver::ConsumeClosedConnections()
{
    std::vector<FNetConnectionId> Result = std::move(ClosedConnections);
    ClosedConnections.clear();
    return Result;
}

void FNetDriver::SetActorOwningConnection(
    PActor* Actor, FNetConnectionId ConnectionId)
{
    ReplicationSystem.SetActorOwningConnection(Actor, ConnectionId);
}

FNetConnectionId FNetDriver::GetActorOwningConnection(
    const PActor* Actor) const
{
    return ReplicationSystem.GetActorOwningConnection(Actor);
}

bool FNetDriver::CallRemoteFunction(
    PActor* Target,
    FName FunctionName,
    std::span<const FFunctionValue> Arguments)
{
    if (Target == nullptr || FunctionName.IsNone()) return false;
    const PFunction* Function = Target->GetClass()->FindFunction(FunctionName);
    if (Function == nullptr) return false;
    const EFunctionFlags Flags = Function->GetFlags();
    const bool bServer = HasAnyFlags(Flags, EFunctionFlags::Server);
    const bool bClient = HasAnyFlags(Flags, EFunctionFlags::Client);
    const bool bMulticast = HasAnyFlags(Flags, EFunctionFlags::NetMulticast);
    if (static_cast<int>(bServer) + static_cast<int>(bClient)
            + static_cast<int>(bMulticast) != 1)
        return false;
    if (NetMode == ENetMode::Standalone)
        return Target->ProcessEvent(Function, Arguments)
            == EFunctionInvokeResult::Success;

    std::vector<uint8> Message;
    if (!ReplicationSystem.BuildRpcMessage(
            Target, FunctionName, Arguments, Message)) return false;
    const bool bReliable = HasAnyFlags(Flags, EFunctionFlags::Reliable);
    const auto Queue = [&](FNetConnection& Connection)
    {
        return bReliable
            ? Connection.QueueReliable(Message)
            : Connection.QueueUnreliable(Message);
    };

    bool bQueued = false;
    if (NetMode == ENetMode::Client)
    {
        if (!bServer || Target->GetLocalRole() != ENetRole::AutonomousProxy
            || Connections.empty()) return false;
        bQueued = Queue(*Connections.front());
    }
    else if (NetMode == ENetMode::Server && bClient)
    {
        const FNetConnectionId Owner = GetActorOwningConnection(Target);
        for (const std::unique_ptr<FNetConnection>& Connection : Connections)
        {
            if (Connection->GetConnectionId() == Owner
                && Connection->GetState() == ENetConnectionState::Open)
            {
                bQueued = Queue(*Connection);
                break;
            }
        }
    }
    else if (NetMode == ENetMode::Server && bMulticast)
    {
        const bool bLocalExecuted = Target->ProcessEvent(Function, Arguments)
            == EFunctionInvokeResult::Success;
        for (const std::unique_ptr<FNetConnection>& Connection : Connections)
        {
            if (Connection->GetState() == ENetConnectionState::Open)
                bQueued = Queue(*Connection) || bQueued;
        }
        bQueued = bQueued || bLocalExecuted;
    }
    if (bQueued) ReplicationSystem.RecordRpcSent();
    return bQueued;
}

bool FNetDriver::QueueCharacterMoves(
    PCharacter* Character,
    std::span<const FCharacterNetworkMove> Moves)
{
    if (NetMode != ENetMode::Client || Character == nullptr
        || Character->GetLocalRole() != ENetRole::AutonomousProxy
        || Moves.empty() || Connections.empty())
        return false;
    FNetConnection* Connection = Connections.front().get();
    if (Connection == nullptr
        || Connection->GetState() != ENetConnectionState::Open)
        return false;
    std::vector<uint8> Message;
    if (!ReplicationSystem.BuildCharacterMoveMessage(
            Character, Moves, Message))
        return false;
    if (!Connection->QueueUnreliable(Message)) return false;
    ReplicationSystem.RecordCharacterMovesSent(Moves.size());
    return true;
}

void FNetDriver::SetNetworkSimulationSettings(
    const FNetworkSimulationSettings& Settings)
{
    NetworkSimulation.LatencyMs = std::clamp(Settings.LatencyMs, 0, 2000);
    NetworkSimulation.JitterMs = std::clamp(Settings.JitterMs, 0, 1000);
    NetworkSimulation.PacketLossPercent = std::clamp(
        Settings.PacketLossPercent, 0, 100);
}

FNetworkSimulationSnapshot FNetDriver::GetNetworkSimulationSnapshot() const
{
    return {NetworkSimulation, DelayedPackets.size(),
        SimulatedDroppedPacketCount};
}

uint32 FNetDriver::NextSimulationRandom()
{
    SimulationRandomState = SimulationRandomState * 1664525u + 1013904223u;
    return SimulationRandomState;
}

void FNetDriver::SendOrDelayPacket(
    const FNetAddress& RemoteAddress,
    std::span<const uint8> Bytes)
{
    if (Transport == nullptr || Bytes.empty()) return;
    if (NetworkSimulation.PacketLossPercent > 0
        && static_cast<int>(NextSimulationRandom() % 100u)
            < NetworkSimulation.PacketLossPercent)
    {
        ++SimulatedDroppedPacketCount;
        return;
    }
    int DelayMs = NetworkSimulation.LatencyMs;
    if (NetworkSimulation.JitterMs > 0)
    {
        const int Range = NetworkSimulation.JitterMs * 2 + 1;
        DelayMs += static_cast<int>(NextSimulationRandom()
            % static_cast<uint32>(Range)) - NetworkSimulation.JitterMs;
    }
    DelayMs = std::max(DelayMs, 0);
    if (DelayMs == 0)
    {
        if (!Transport->SendTo(RemoteAddress, Bytes))
            LastError = Transport->GetLastError();
        return;
    }
    if (DelayedPackets.size() >= MaxDelayedPackets)
    {
        ++SimulatedDroppedPacketCount;
        return;
    }
    FDelayedPacket Packet;
    Packet.RemoteAddress = RemoteAddress;
    Packet.Bytes.assign(Bytes.begin(), Bytes.end());
    Packet.DeliveryTime = ElapsedSeconds
        + static_cast<double>(DelayMs) / 1000.0;
    DelayedPackets.push_back(std::move(Packet));
}

void FNetDriver::FlushDelayedPackets()
{
    if (Transport == nullptr) return;
    for (auto It = DelayedPackets.begin(); It != DelayedPackets.end();)
    {
        if (It->DeliveryTime > ElapsedSeconds)
        {
            ++It;
            continue;
        }
        if (!Transport->SendTo(It->RemoteAddress, It->Bytes))
            LastError = Transport->GetLastError();
        It = DelayedPackets.erase(It);
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
