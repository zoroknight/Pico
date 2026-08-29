#pragma once

#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/TickFunction.h"
#include "Pico/Net/NetTypes.h"
#include "Pico/Engine/NetRole.h"
#include "Pico/Object/ObjectDelegate.h"
#include "Pico/Object/ReflectionMacros.h"

#include <string_view>
#include <array>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace Pico
{
class PActor;
class PLevel;
class PSceneComponent;
class PWorld;
class FWorldAssetLoader;
class FReferenceCollector;

using FOnActorDestroyed = TObjectMulticastDelegate<void(PActor*)>;

class PActor : public PObject
{
    PICO_DECLARE_CLASS(PActor, PObject)

public:
    FActorTickFunction PrimaryActorTick;

    PWorld* GetWorld() const;
    PLevel* GetLevel() const;
    PActor* GetOwner() const;
    bool GetIsReplicated() const;
    void SetReplicates(bool bInReplicates);
    FNetObjectId GetNetObjectId() const;
    ENetRole GetLocalRole() const;
    ENetRole GetRemoteRole() const;
    bool IsOnlyRelevantToOwner() const;
    void SetOnlyRelevantToOwner(bool bValue);
    uint64 GetReplicationGeneration() const;
    uint64 GetReplicationDirtyMask() const;
    void MarkReplicationDirty();
    bool MarkReplicatedPropertyDirty(FName PropertyName);
    void GetReplicationDirtyStateSince(
        uint64 LastObservedGeneration,
        uint64& OutDirtyMask,
        bool& OutTransformDirty) const;
    bool HasBegunPlay() const;
    bool IsPendingDestroy() const;
    bool Destroy();
    FOnActorDestroyed& OnDestroyed();
    void RegisterAllComponents();
    void UnregisterAllComponents();

    PActorComponent* CreateComponent(const PClass* ComponentClass, FName Name);
    PActorComponent* CreateComponent(const PClass* ComponentClass, std::string_view Name);
    bool DestroyComponent(PActorComponent* Component);
    bool CanDestroyBlueprintComponent(const PActorComponent* Component) const;
    bool DestroyBlueprintComponent(PActorComponent* Component);

    template <typename TComponent>
    TComponent* CreateComponent(FName Name)
    {
        static_assert(
            std::is_base_of_v<PActorComponent, TComponent>,
            "CreateComponent only constructs PActorComponent-derived types");
        return static_cast<TComponent*>(CreateComponent(TComponent::StaticClass(), Name));
    }

    template <typename TComponent>
    TComponent* CreateComponent(std::string_view Name)
    {
        return CreateComponent<TComponent>(FName(Name));
    }

    std::vector<PActorComponent*> GetComponents() const;
    bool SynchronizeDefaultSubobjects(std::size_t* OutAddedCount = nullptr);
    PSceneComponent* GetRootComponent() const;
    bool SetRootComponent(PSceneComponent* Component);

    FTransform GetActorTransform() const;
    bool SetActorTransform(const FTransform& Transform);
    FVector3 GetActorLocation() const;
    bool SetActorLocation(const FVector3& Location);
    FRotator GetActorRotation() const;
    bool SetActorRotation(const FRotator& Rotation);
    FVector3 GetActorScale() const;
    bool SetActorScale(const FVector3& Scale);

    virtual void BeginPlay();
    virtual void Tick(float DeltaSeconds);
    virtual void EndPlay();

protected:
    explicit PActor(const FObjectConstructionParams& Params);
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
    void BeginDestroy() override;
    void AddReferencedObjects(FReferenceCollector& Collector) const override;
    bool OnDefaultSubobjectCreated(PObject* Subobject) override;
    bool OnDefaultSubobjectRelation(
        PObject* Subobject,
        PObject* AttachParent,
        FName AttachSocketName,
        bool bIsRoot) override;

private:
    void DispatchBeginPlay();
    void DispatchTick(float DeltaSeconds);
    void DispatchEndPlay();
    void DispatchDestroyed();
    void MarkPendingDestroy();
    void SetOwner(PActor* InOwner);
    void SetNetObjectId(FNetObjectId InNetObjectId);
    void SetNetRoles(ENetRole InLocalRole, ENetRole InRemoteRole);
    void RecordReplicationDirty(uint64 DirtyMask, bool bTransformDirty);
    PActorComponent* ResolveComponent(FObjectHandle Handle) const;
    bool OwnsComponent(const PActorComponent* Component) const;
    friend class PWorld;
    friend class FWorldAssetLoader;
    friend class FActorTickFunction;
    friend class FReplicationSystem;
    friend class FNetObjectRegistry;

    std::vector<FObjectHandle> ComponentHandles;
    FObjectHandle RootComponentHandle;
    FObjectHandle OwnerHandle;
    FNetObjectId NetObjectId;
    FOnActorDestroyed ActorDestroyedEvent;
    bool bHasBegunPlay = false;
    bool bHasEndedPlay = false;
    bool bPendingDestroy = false;
    bool bReplicates = false;
    bool bOnlyRelevantToOwner = false;
    ENetRole LocalRole = ENetRole::Authority;
    ENetRole RemoteRole = ENetRole::SimulatedProxy;
    struct FReplicationDirtyRecord
    {
        uint64 Generation = 0;
        uint64 DirtyMask = 0;
        bool bTransformDirty = false;
    };
    static constexpr std::size_t ReplicationDirtyHistorySize = 8;
    uint64 ReplicationGeneration = 1;
    uint64 ReplicationDirtyMask = ~uint64 {0};
    bool bReplicationTransformDirty = true;
    std::array<FReplicationDirtyRecord, ReplicationDirtyHistorySize>
        ReplicationDirtyHistory {};
    std::size_t ReplicationDirtyHistoryWriteIndex = 0;
    bool bDestroyedEventBroadcast = false;
};
}
