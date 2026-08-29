#pragma once

#include "Pico/Net/NetTypes.h"
#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Object/ObjectTypes.h"
#include "Pico/Object/Property.h"
#include "Pico/Object/Function.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Pico
{
class PActor;
class PClass;
class PObject;
class PWorld;

enum class EActorChannelState : uint8
{
    PendingOpen,
    Open,
    PendingClose,
    Closed
};

struct FReplicationFieldDescriptor
{
    uint16 FieldId = 0;
    const PProperty* Property = nullptr;
    EReplicationCondition Condition = EReplicationCondition::Always;
    FName RepNotifyFunction;
};

class FReplicationSchema
{
public:
    static FReplicationSchema Build(const PClass* Class);

    const PClass* GetClass() const { return Class; }
    uint32 GetHash() const { return Hash; }
    const std::vector<FReplicationFieldDescriptor>& GetFields() const
    {
        return Fields;
    }
    const FReplicationFieldDescriptor* FindField(uint16 FieldId) const;
    std::size_t GetStorageBytes() const
    {
        return Fields.size() * sizeof(FReplicationFieldDescriptor);
    }
    std::size_t GetReservedStorageBytes() const
    {
        return Fields.capacity() * sizeof(FReplicationFieldDescriptor);
    }

private:
    const PClass* Class = nullptr;
    uint32 Hash = 0;
    std::vector<FReplicationFieldDescriptor> Fields;
};

class FNetObjectRegistry
{
public:
    FNetObjectId RegisterAuthorityObject(PActor* Actor);
    bool RegisterRemoteObject(FNetObjectId NetId, PActor* Actor);
    FNetObjectId FindNetId(FObjectHandle Handle) const;
    PActor* ResolveActor(FNetObjectId NetId) const;
    bool RemoveByNetId(FNetObjectId NetId);
    void Reset();
    std::size_t Num() const { return Entries.size(); }

private:
    struct FEntry
    {
        FNetObjectId NetId;
        FObjectHandle Handle;
    };

    std::vector<FEntry> Entries;
    FNetObjectId NextAuthorityId {1};
};

struct FActorChannelSnapshot
{
    FNetConnectionId ConnectionId;
    FNetObjectId NetObjectId;
    FObjectHandle ActorHandle;
    EActorChannelState State = EActorChannelState::Closed;
    std::size_t BaselineFieldCount = 0;
    uint32 PendingReliableId = 0;
};

struct FReplicationStatistics
{
    uint64 SpawnMessagesSent = 0;
    uint64 DeltaMessagesSent = 0;
    uint64 DestroyMessagesSent = 0;
    uint64 MessagesReceived = 0;
    uint64 RejectedMessages = 0;
    uint64 OnRepCalls = 0;
    uint64 RpcMessagesSent = 0;
    uint64 RpcMessagesReceived = 0;
    uint64 RpcMessagesRejected = 0;
    uint64 CharacterMoveMessagesSent = 0;
    uint64 CharacterMoveMessagesReceived = 0;
    uint64 CharacterMoveMessagesRejected = 0;
    uint64 CharacterCorrectionsSent = 0;
    uint64 CharacterCorrectionsReceived = 0;
    uint64 CharacterSnapshotsSent = 0;
    uint64 CharacterSnapshotsReceived = 0;
    std::size_t ChannelCount = 0;
    std::size_t NetObjectCount = 0;
    std::size_t UnresolvedReferenceCount = 0;
};

class FReplicationSystem
{
public:
    struct FImpl;
    FReplicationSystem() = default;
    FReplicationSystem(const FReplicationSystem&) = delete;
    FReplicationSystem& operator=(const FReplicationSystem&) = delete;
    FReplicationSystem(FReplicationSystem&&) noexcept = default;
    FReplicationSystem& operator=(FReplicationSystem&&) noexcept = default;
    using FQueueReliable =
        std::function<bool(std::span<const uint8>, uint32*)>;
    using FQueueUnreliable = std::function<bool(std::span<const uint8>)>;

    void SetWorld(PWorld* InWorld);
    void BeginNetworkFrame();
    void Reset();
    void ReplicateServerConnection(
        FNetConnectionId ConnectionId,
        const FQueueReliable& QueueReliable,
        const FQueueUnreliable& QueueUnreliable = {});
    bool HandleReliableMessage(
        FNetConnectionId ConnectionId,
        std::span<const uint8> Payload);
    bool HandleUnreliableMessage(
        FNetConnectionId ConnectionId,
        std::span<const uint8> Payload);
    void HandleReliableAcknowledged(
        FNetConnectionId ConnectionId,
        uint32 ReliableId);
    void HandleConnectionClosed(FNetConnectionId ConnectionId);

    const FNetObjectRegistry& GetObjectRegistry() const { return ObjectRegistry; }
    std::vector<FActorChannelSnapshot> GetChannelSnapshots() const;
    FReplicationStatistics GetStatistics() const;
    void PublishMemoryStatistics() const;
    void SetActorOwningConnection(PActor* Actor, FNetConnectionId ConnectionId);
    FNetConnectionId GetActorOwningConnection(const PActor* Actor) const;
    bool BuildRpcMessage(
        PActor* Target,
        FName FunctionName,
        std::span<const FFunctionValue> Arguments,
        std::vector<uint8>& OutMessage);
    void RecordRpcSent();
    bool BuildCharacterMoveMessage(
        PActor* Target,
        std::span<const FCharacterNetworkMove> Moves,
        std::vector<uint8>& OutMessage);
    void RecordCharacterMovesSent(std::size_t MoveCount);

private:
    std::shared_ptr<FImpl> Impl;
    PWorld* World = nullptr;
    FNetObjectRegistry ObjectRegistry;
    FReplicationStatistics Statistics;
    bool HandleMessage(
        FNetConnectionId ConnectionId,
        std::span<const uint8> Payload,
        bool bReliable);
};
}
