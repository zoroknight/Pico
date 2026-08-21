#pragma once

#include "Pico/Net/NetConnection.h"
#include "Pico/Net/NetTransport.h"
#include "Pico/Engine/Replication.h"
#include "Pico/Engine/NetRole.h"

#include <memory>
#include <vector>

namespace Pico
{
class PWorld;
class PActor;

struct FNetConnectionSnapshot
{
    FNetConnectionId ConnectionId;
    FNetAddress RemoteAddress;
    ENetConnectionState State = ENetConnectionState::Closed;
    FNetStatistics Statistics;
    std::size_t PendingReliableMessages = 0;
    std::string CloseReason;
};

class FNetDriver
{
public:
    bool InitializeFromCommandLine();
    bool InitializeServer(
        uint16 Port,
        std::unique_ptr<INetTransport> InTransport = {});
    bool InitializeClient(
        const FNetAddress& ServerAddress,
        std::unique_ptr<INetTransport> InTransport = {});
    void Shutdown();

    void TickDispatch(float DeltaSeconds);
    void TickFlush(float DeltaSeconds);
    bool QueueReliableToAll(std::span<const uint8> Payload);
    void SetWorld(PWorld* World);

    ENetMode GetNetMode() const { return NetMode; }
    bool IsInitialized() const { return bInitialized; }
    const FNetAddress& GetLocalAddress() const;
    std::vector<FNetConnectionSnapshot> GetConnectionSnapshots() const;
    std::size_t GetOpenConnectionCount() const;
    uint64 GetInvalidPacketCount() const { return InvalidPacketCount; }
    const std::string& GetLastError() const { return LastError; }
    std::vector<FActorChannelSnapshot> GetActorChannelSnapshots() const;
    FReplicationStatistics GetReplicationStatistics() const;
    std::vector<FNetConnectionId> ConsumeOpenedConnections();
    std::vector<FNetConnectionId> ConsumeClosedConnections();
    void SetActorOwningConnection(PActor* Actor, FNetConnectionId ConnectionId);
    FNetConnectionId GetActorOwningConnection(const PActor* Actor) const;
    bool CallRemoteFunction(
        PActor* Target,
        FName FunctionName,
        std::span<const FFunctionValue> Arguments = {});

private:
    FNetConnection* FindConnection(
        FNetConnectionId ConnectionId,
        const FNetAddress& RemoteAddress) const;
    FNetConnection* FindConnectionByAddress(
        const FNetAddress& RemoteAddress) const;
    void DispatchPacket(
        const FNetAddress& RemoteAddress,
        const FDecodedNetPacket& Packet);
    uint64 MakeClientNonce() const;

    std::unique_ptr<INetTransport> Transport;
    std::vector<std::unique_ptr<FNetConnection>> Connections;
    ENetMode NetMode = ENetMode::Standalone;
    FNetConnectionId NextConnectionId { 1 };
    double ElapsedSeconds = 0.0;
    uint64 InvalidPacketCount = 0;
    std::string LastError;
    FReplicationSystem ReplicationSystem;
    std::vector<FNetConnectionId> OpenedConnections;
    std::vector<FNetConnectionId> ClosedConnections;
    bool bInitialized = false;
};
}
