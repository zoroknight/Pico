#include "TestRunner.h"

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Replication.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectInitializer.h"
#include "Pico/Object/ObjectPtr.h"

#include <utility>
#include <vector>

namespace
{
class PReplicationTestActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PReplicationTestActor, Pico::PActor)

public:
    void SetValue(Pico::int32 InValue) { Value = InValue; }
    Pico::int32 GetValue() const { return Value; }
    void SetInitialValue(Pico::int32 InValue) { InitialValue = InValue; }
    Pico::int32 GetInitialValue() const { return InitialValue; }
    void SetTarget(PReplicationTestActor* InTarget) { Target = InTarget; }
    PReplicationTestActor* GetTarget() const { return Target.Get(); }
    int GetRepNotifyCount() const { return RepNotifyCount; }
    void OnRep_Value() { ++RepNotifyCount; }

protected:
    explicit PReplicationTestActor(
        const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
        SetReplicates(true);
    }

    bool DefineDefaultSubobjects(
        Pico::FObjectInitializer& Initializer) override
    {
        Pico::PSceneComponent* Root =
            Initializer.CreateDefaultSubobject<Pico::PSceneComponent>("Root");
        return Root != nullptr && Initializer.SetRootSubobject(Root);
    }

private:
    Pico::int32 Value = 0;
    Pico::int32 InitialValue = 0;
    Pico::TObjectPtr<PReplicationTestActor> Target;
    int RepNotifyCount = 0;
};

PICO_DEFINE_CLASS(PReplicationTestActor)

bool PReplicationTestActor::RegisterProperties(Pico::PClass& Class)
{
    Pico::FPropertyMetadata ValueMetadata;
    ValueMetadata.Flags = Pico::EPropertyFlags::Transient
        | Pico::EPropertyFlags::Replicated;
    ValueMetadata.RepNotifyFunction = Pico::FName("OnRep_Value");
    Pico::FPropertyMetadata InitialMetadata = ValueMetadata;
    InitialMetadata.RepNotifyFunction = {};
    InitialMetadata.ReplicationCondition =
        Pico::EReplicationCondition::InitialOnly;
    Pico::FPropertyMetadata TargetMetadata = ValueMetadata;
    TargetMetadata.RepNotifyFunction = {};

    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Value, ValueMetadata);
    PICO_ADD_PROPERTY_METADATA(Properties, InitialValue, InitialMetadata);
    PICO_ADD_PROPERTY_METADATA(Properties, Target, TargetMetadata);
    if (!Class.AddProperties(std::move(Properties))) return false;

    std::vector<Pico::PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions,
        OnRep_Value,
        Pico::EFunctionFlags::Callable);
    return Class.AddFunctions(std::move(Functions));
}

class PRootlessReplicationActor : public Pico::PActor
{
    PICO_DECLARE_CLASS(PRootlessReplicationActor, Pico::PActor)

protected:
    explicit PRootlessReplicationActor(
        const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
        SetReplicates(true);
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PRootlessReplicationActor)

struct FQueuedMessage
{
    Pico::uint32 ReliableId = 0;
    std::vector<Pico::uint8> Payload;
};

void TestReplicationLifecycle(FTestRunner& Runner)
{
    char Program[] = "PicoReplicationTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = {Program, MaxFPS};
    Pico::FEngineLoop EngineLoop;
    const bool bInitialized = EngineLoop.PreInit(2, Arguments) == 0
        && EngineLoop.Init() == 0
        && PReplicationTestActor::RegisterClass()
        && PRootlessReplicationActor::RegisterClass();
    Runner.Expect(bInitialized, "Replication test initializes engine classes");
    if (!bInitialized)
    {
        EngineLoop.Exit();
        return;
    }

    Pico::PWorld* ServerWorld = EngineLoop.GetWorld();
    Pico::PWorld* ClientWorld = Pico::NewObject<Pico::PWorld>(
        nullptr, "ReplicationClientWorld", Pico::EObjectFlags::RootSet);
    const bool bWorldsReady = ServerWorld != nullptr
        && ClientWorld != nullptr && ClientWorld->Initialize();
    Runner.Expect(bWorldsReady, "Replication test creates independent Worlds");
    if (!bWorldsReady)
    {
        if (ClientWorld != nullptr) Pico::DestroyObjectTree(ClientWorld);
        EngineLoop.Exit();
        return;
    }

    auto* Source = ServerWorld->SpawnActor<PReplicationTestActor>("Source");
    auto* Target = ServerWorld->SpawnActor<PReplicationTestActor>("Target");
    Runner.Expect(Source != nullptr && Target != nullptr,
        "Server creates replicated actors");
    if (Source == nullptr || Target == nullptr)
    {
        ClientWorld->TearDown();
        Pico::DestroyObjectTree(ClientWorld);
        EngineLoop.Exit();
        return;
    }
    Source->SetValue(10);
    Source->SetInitialValue(7);
    Source->SetTarget(Target);
    Source->SetActorLocation(Pico::FVector3(25.0f, -10.0f, 5.0f));

    Pico::FReplicationSystem ServerReplication;
    Pico::FReplicationSystem ClientReplication;
    ServerReplication.SetWorld(ServerWorld);
    ClientReplication.SetWorld(ClientWorld);
    const Pico::FNetConnectionId Connection {1};
    Pico::uint32 NextReliableId = 1;
    std::vector<FQueuedMessage> Messages;
    const auto Queue = [&](std::span<const Pico::uint8> Payload,
        Pico::uint32* OutReliableId)
    {
        const Pico::uint32 ReliableId = NextReliableId++;
        if (OutReliableId != nullptr) *OutReliableId = ReliableId;
        Messages.push_back({ReliableId,
            std::vector<Pico::uint8>(Payload.begin(), Payload.end())});
        return true;
    };

    ServerReplication.ReplicateServerConnection(Connection, Queue);
    Runner.Expect(Messages.size() == 2,
        "A new connection receives one Spawn per replicated actor");
    const Pico::FNetObjectId SourceId = Source->GetNetObjectId();
    const Pico::FNetObjectId TargetId = Target->GetNetObjectId();

    std::vector<Pico::uint8> TruncatedSpawn = Messages[0].Payload;
    TruncatedSpawn.pop_back();
    Runner.Expect(!ClientReplication.HandleReliableMessage(
            Connection, TruncatedSpawn)
        && ClientReplication.GetObjectRegistry().ResolveActor(SourceId)
            == nullptr,
        "A malformed Spawn is rejected before creating an Actor");

    Runner.Expect(ClientReplication.HandleReliableMessage(
        Connection, Messages[0].Payload), "Client applies the first Spawn");
    auto* ClientSource = static_cast<PReplicationTestActor*>(
        ClientReplication.GetObjectRegistry().ResolveActor(SourceId));
    Runner.Expect(ClientSource != nullptr
        && ClientSource->GetValue() == 10
        && ClientSource->GetInitialValue() == 7
        && ClientSource->GetTarget() == nullptr
        && ClientSource->GetRepNotifyCount() == 1
        && ClientSource->GetActorLocation().Equals(
            Pico::FVector3(25.0f, -10.0f, 5.0f)),
        "Spawn applies reflected values, Transform, and OnRep");
    Runner.Expect(
        ClientReplication.GetStatistics().UnresolvedReferenceCount == 1,
        "An object reference waits when its target Spawn has not arrived");

    Runner.Expect(ClientReplication.HandleReliableMessage(
        Connection, Messages[1].Payload), "Client applies the target Spawn");
    auto* ClientTarget = static_cast<PReplicationTestActor*>(
        ClientReplication.GetObjectRegistry().ResolveActor(TargetId));
    Runner.Expect(ClientTarget != nullptr
        && ClientSource->GetTarget() == ClientTarget
        && ClientReplication.GetStatistics().UnresolvedReferenceCount == 0,
        "A later Spawn repairs pending object references");

    for (const FQueuedMessage& Message : Messages)
    {
        ServerReplication.HandleReliableAcknowledged(
            Connection, Message.ReliableId);
    }
    Messages.clear();
    ServerReplication.ReplicateServerConnection(Connection, Queue);
    Runner.Expect(Messages.empty(),
        "Acknowledged unchanged properties produce no Delta");

    Source->SetValue(20);
    Source->SetInitialValue(99);
    Source->SetActorLocation(Pico::FVector3(50.0f, 0.0f, 5.0f));
    ServerReplication.ReplicateServerConnection(Connection, Queue);
    Runner.Expect(Messages.size() == 1
        && ClientReplication.HandleReliableMessage(
            Connection, Messages.front().Payload),
        "A changed actor produces one Delta");
    Runner.Expect(ClientSource->GetValue() == 20
        && ClientSource->GetInitialValue() == 7
        && ClientSource->GetRepNotifyCount() == 2
        && ClientSource->GetActorLocation().Equals(
            Pico::FVector3(50.0f, 0.0f, 5.0f)),
        "Delta updates changed state while InitialOnly remains unchanged");
    ServerReplication.HandleReliableAcknowledged(
        Connection, Messages.front().ReliableId);
    Messages.clear();

    const Pico::FNetConnectionId SecondConnection {2};
    ServerReplication.ReplicateServerConnection(SecondConnection, Queue);
    Runner.Expect(Messages.size() == 2,
        "A second connection owns an independent initial baseline");
    ServerReplication.HandleConnectionClosed(SecondConnection);
    Runner.Expect(ServerReplication.GetStatistics().ChannelCount == 2,
        "Disconnect removes only that connection's ActorChannels");
    Messages.clear();

    Runner.Expect(ServerWorld->DestroyActor(Target),
        "Server destroys the referenced actor");
    ServerReplication.ReplicateServerConnection(Connection, Queue);
    Runner.Expect(!Messages.empty(),
        "Destroy and reference changes are queued reliably");
    for (const FQueuedMessage& Message : Messages)
    {
        ClientReplication.HandleReliableMessage(Connection, Message.Payload);
        ServerReplication.HandleReliableAcknowledged(
            Connection, Message.ReliableId);
    }
    Runner.Expect(ClientReplication.GetObjectRegistry().ResolveActor(TargetId)
            == nullptr
        && ClientSource->GetTarget() == nullptr,
        "Destroy removes the remote Actor and leaves no live object reference");

    Messages.clear();
    auto* Rootless =
        ServerWorld->SpawnActor<PRootlessReplicationActor>("Rootless");
    ServerReplication.ReplicateServerConnection(Connection, Queue);
    const Pico::FNetObjectId RootlessId = Rootless != nullptr
        ? Rootless->GetNetObjectId() : Pico::FNetObjectId {};
    bool bAppliedRootlessSpawn = false;
    for (const FQueuedMessage& Message : Messages)
    {
        bAppliedRootlessSpawn = ClientReplication.HandleReliableMessage(
            Connection, Message.Payload) || bAppliedRootlessSpawn;
        ServerReplication.HandleReliableAcknowledged(
            Connection, Message.ReliableId);
    }
    Runner.Expect(Rootless != nullptr && bAppliedRootlessSpawn
        && ClientReplication.GetObjectRegistry().ResolveActor(RootlessId)
            != nullptr,
        "A replicated Gameplay Actor without a RootComponent skips Transform safely");

    ServerReplication.Reset();
    ClientReplication.Reset();
    ClientWorld->TearDown();
    Pico::DestroyObjectTree(ClientWorld);
    EngineLoop.Exit();
}
}

int main()
{
    FTestRunner Runner;
    TestReplicationLifecycle(Runner);
    return Runner.Finish();
}
