#include "Pico/Engine/Actor.h"

#include "Pico/Core/Log.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/Property.h"
#include "Pico/Object/ReferenceCollector.h"

#include <algorithm>
#include <exception>
#include <functional>

namespace Pico
{
PICO_DEFINE_CLASS(PActor)

bool PActor::RegisterProperties(PClass& Class)
{
    FPropertyMetadata ReplicatesMetadata;
    ReplicatesMetadata.DisplayName = "Replicates";
    ReplicatesMetadata.Description =
        "Create an ActorChannel and replicate this Actor from the server";
    FPropertyMetadata MovementMetadata;
    MovementMetadata.DisplayName = "Replicate Movement";
    MovementMetadata.Description =
        "Replicate the root Transform or root rigid-body state from the server";
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, bReplicates, ReplicatesMetadata);
    PICO_ADD_PROPERTY_METADATA(
        Properties, bReplicateMovement, MovementMetadata);
    return Class.AddProperties(std::move(Properties));
}

PActor::PActor(const FObjectConstructionParams& Params)
    : PObject(Params)
{
    PrimaryActorTick.SetCanEverTick(true);
    PrimaryActorTick.SetTickEnabled(true);
}

void PActor::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PObject::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(ComponentHandles);
    Collector.AddReferencedHandle(RootComponentHandle);
    Collector.AddReferencedHandle(OwnerHandle);
}

PWorld* PActor::GetWorld() const
{
    PLevel* Level = GetLevel();
    return Level != nullptr ? Level->GetWorld() : nullptr;
}

PLevel* PActor::GetLevel() const
{
    PObject* Outer = GetOuter();
    return Outer != nullptr && Outer->IsA(PLevel::StaticClass())
        ? static_cast<PLevel*>(Outer)
        : nullptr;
}

PActor* PActor::GetOwner() const
{
    PObject* Object = ResolveObject(OwnerHandle);
    return Object != nullptr && Object->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Object)
        : nullptr;
}

bool PActor::GetIsReplicated() const
{
    return bReplicates;
}

void PActor::SetReplicates(bool bInReplicates)
{
    if (bReplicates == bInReplicates) return;
    bReplicates = bInReplicates;
    if (bReplicates) MarkReplicationDirty();
}

bool PActor::GetReplicateMovement() const
{
    return bReplicateMovement;
}

void PActor::SetReplicateMovement(bool bInReplicateMovement)
{
    if (bReplicateMovement == bInReplicateMovement) return;
    bReplicateMovement = bInReplicateMovement;
    RecordReplicationDirty(0, true);
}

FNetObjectId PActor::GetNetObjectId() const
{
    return NetObjectId;
}

ENetRole PActor::GetLocalRole() const { return LocalRole; }
ENetRole PActor::GetRemoteRole() const { return RemoteRole; }
bool PActor::IsOnlyRelevantToOwner() const { return bOnlyRelevantToOwner; }
void PActor::SetOnlyRelevantToOwner(bool bValue) { bOnlyRelevantToOwner = bValue; }

uint64 PActor::GetReplicationGeneration() const
{
    return ReplicationGeneration;
}

uint64 PActor::GetReplicationDirtyMask() const
{
    return ReplicationDirtyMask;
}

void PActor::MarkReplicationDirty()
{
    RecordReplicationDirty(~uint64 {0}, true);
}

bool PActor::MarkReplicatedPropertyDirty(FName PropertyName)
{
    if (PropertyName.IsNone() || GetClass() == nullptr) return false;
    std::vector<const PClass*> Hierarchy;
    for (const PClass* Current = GetClass(); Current != nullptr;
        Current = Current->GetSuperClass())
    {
        Hierarchy.push_back(Current);
    }
    std::reverse(Hierarchy.begin(), Hierarchy.end());

    uint16 FieldIndex = 0;
    for (const PClass* Current : Hierarchy)
    {
        for (const PProperty& Property : Current->GetProperties())
        {
            if (!Property.HasAnyFlags(EPropertyFlags::Replicated)
                || Property.GetType() == EPropertyType::DynamicMulticastDelegate)
            {
                continue;
            }
            if (FieldIndex >= 64) return false;
            if (Property.GetName() == PropertyName)
            {
                RecordReplicationDirty(
                    uint64 {1} << FieldIndex, false);
                return true;
            }
            ++FieldIndex;
        }
    }
    return false;
}

void PActor::GetReplicationDirtyStateSince(
    uint64 LastObservedGeneration,
    uint64& OutDirtyMask,
    bool& OutTransformDirty) const
{
    OutDirtyMask = 0;
    OutTransformDirty = false;
    if (LastObservedGeneration == ReplicationGeneration) return;
    if (LastObservedGeneration > ReplicationGeneration
        || ReplicationGeneration - LastObservedGeneration
            > ReplicationDirtyHistorySize)
    {
        OutDirtyMask = ~uint64 {0};
        OutTransformDirty = true;
        return;
    }

    std::size_t FoundCount = 0;
    for (const FReplicationDirtyRecord& Record : ReplicationDirtyHistory)
    {
        if (Record.Generation > LastObservedGeneration
            && Record.Generation <= ReplicationGeneration)
        {
            OutDirtyMask |= Record.DirtyMask;
            OutTransformDirty |= Record.bTransformDirty;
            ++FoundCount;
        }
    }
    if (FoundCount != ReplicationGeneration - LastObservedGeneration)
    {
        OutDirtyMask = ~uint64 {0};
        OutTransformDirty = true;
    }
}

void PActor::RecordReplicationDirty(uint64 DirtyMask, bool bTransformDirty)
{
    if (++ReplicationGeneration == 0)
    {
        ReplicationGeneration = 1;
        ReplicationDirtyHistory = {};
        ReplicationDirtyHistoryWriteIndex = 0;
    }
    ReplicationDirtyMask = DirtyMask;
    bReplicationTransformDirty = bTransformDirty;
    ReplicationDirtyHistory[ReplicationDirtyHistoryWriteIndex] = {
        ReplicationGeneration, DirtyMask, bTransformDirty};
    ReplicationDirtyHistoryWriteIndex =
        (ReplicationDirtyHistoryWriteIndex + 1)
        % ReplicationDirtyHistorySize;
}

void PActor::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    PObject::PostEditChangeProperty(Event);
    if (Event.Property == nullptr) return;
    const FName PropertyName = Event.Property->GetName();
    if (PropertyName == FName("bReplicates")
        || PropertyName == FName("bReplicateMovement"))
    {
        RecordReplicationDirty(0, true);
        return;
    }
    MarkReplicatedPropertyDirty(PropertyName);
}

bool PActor::HasBegunPlay() const
{
    return bHasBegunPlay;
}

bool PActor::IsPendingDestroy() const
{
    return bPendingDestroy;
}

bool PActor::Destroy()
{
    PWorld* World = GetWorld();
    return World != nullptr && World->DestroyActor(this);
}

FOnActorDestroyed& PActor::OnDestroyed()
{
    return ActorDestroyedEvent;
}

PActorComponent* PActor::CreateComponent(const PClass* ComponentClass, FName Name)
{
    if (IsBeginningDestroy() || IsPendingDestroy())
    {
        return nullptr;
    }

    if (ComponentClass == nullptr || !ComponentClass->IsChildOf(PActorComponent::StaticClass()))
    {
        return nullptr;
    }

    PObject* Object = NewObject(ComponentClass, this, Name);
    if (Object == nullptr)
    {
        return nullptr;
    }

    PActorComponent* Component = static_cast<PActorComponent*>(Object);
    ComponentHandles.push_back(Component->GetHandle());
    if (HasBegunPlay())
    {
        Component->RegisterComponent();
    }
    return Component;
}

PActorComponent* PActor::CreateComponent(const PClass* ComponentClass, std::string_view Name)
{
    return CreateComponent(ComponentClass, FName(Name));
}

bool PActor::DestroyComponent(PActorComponent* Component)
{
    if (!OwnsComponent(Component)
        || HasAnyFlags(Component->GetFlags(), EObjectFlags::DefaultSubobject))
    {
        return false;
    }

    std::vector<PActorComponent*> ComponentsToDestroy;
    const std::function<void(PActorComponent*)> CollectPostOrder =
        [&ComponentsToDestroy, &CollectPostOrder](PActorComponent* Current)
        {
            if (Current->IsA(PSceneComponent::StaticClass()))
            {
                PSceneComponent* SceneComponent =
                    static_cast<PSceneComponent*>(Current);
                for (PSceneComponent* Child : SceneComponent->GetAttachChildren())
                {
                    CollectPostOrder(Child);
                }
            }
            ComponentsToDestroy.push_back(Current);
        };
    CollectPostOrder(Component);

    for (PActorComponent* ComponentToDestroy : ComponentsToDestroy)
    {
        const FObjectHandle Handle = ComponentToDestroy->GetHandle();
        if (!DestroyObject(ComponentToDestroy))
        {
            return false;
        }

        std::erase(ComponentHandles, Handle);
        if (RootComponentHandle == Handle)
        {
            RootComponentHandle = {};
        }
    }
    return true;
}

bool PActor::CanDestroyBlueprintComponent(
    const PActorComponent* Component) const
{
    if (!OwnsComponent(Component) || Component == GetRootComponent())
        return false;
    if (Component->IsA(PSceneComponent::StaticClass())
        && !static_cast<const PSceneComponent*>(Component)
            ->GetAttachChildren().empty())
        return false;
    if (!HasAnyFlags(Component->GetFlags(), EObjectFlags::DefaultSubobject))
        return true;

    const PClass* ParentClass = GetClass() != nullptr
        ? GetClass()->GetSuperClass() : nullptr;
    if (ParentClass == nullptr) return false;
    const auto& ParentRecords = ParentClass->GetDefaultSubobjects();
    return std::none_of(
        ParentRecords.begin(), ParentRecords.end(),
        [Component](const FDefaultSubobjectRecord& Record)
        { return Record.Name == Component->GetName(); });
}

bool PActor::DestroyBlueprintComponent(PActorComponent* Component)
{
    if (!CanDestroyBlueprintComponent(Component)) return false;
    if (!HasAnyFlags(Component->GetFlags(), EObjectFlags::DefaultSubobject))
        return DestroyComponent(Component);

    const FObjectHandle Handle = Component->GetHandle();
    if (!DestroyObject(Component)) return false;
    std::erase(ComponentHandles, Handle);
    return true;
}

std::vector<PActorComponent*> PActor::GetComponents() const
{
    std::vector<PActorComponent*> Components;
    Components.reserve(ComponentHandles.size());
    for (const FObjectHandle Handle : ComponentHandles)
    {
        if (PActorComponent* Component = ResolveComponent(Handle))
        {
            Components.push_back(Component);
        }
    }
    return Components;
}

bool PActor::SynchronizeDefaultSubobjects(std::size_t* OutAddedCount)
{
    if (OutAddedCount != nullptr) *OutAddedCount = 0;
    if (IsBeginningDestroy() || IsPendingDestroy() || GetClass() == nullptr)
        return false;

    struct FCreatedSubobject
    {
        const FDefaultSubobjectRecord* Record = nullptr;
        PActorComponent* Component = nullptr;
    };
    std::vector<FCreatedSubobject> Created;
    const auto Rollback =
        [this, &Created]()
        {
            for (auto It = Created.rbegin(); It != Created.rend(); ++It)
            {
                const FObjectHandle Handle = It->Component->GetHandle();
                DestroyObject(It->Component);
                std::erase(ComponentHandles, Handle);
                if (RootComponentHandle == Handle) RootComponentHandle = {};
            }
        };

    for (const FDefaultSubobjectRecord& Record : GetClass()->GetDefaultSubobjects())
    {
        PObject* Existing = FindObject(this, Record.Name);
        if (Existing != nullptr)
        {
            if (Existing->GetClass() != Record.Class
                || !Existing->IsA(PActorComponent::StaticClass()))
            {
                Rollback();
                return false;
            }
            continue;
        }

        const FObjectConstructionParams Params {
            Record.Class,
            this,
            Record.Name,
            EObjectFlags::DefaultSubobject,
            Record.Template.get()
        };
        PObject* CreatedObject = NewObject(Params);
        if (CreatedObject == nullptr || !OnDefaultSubobjectCreated(CreatedObject))
        {
            if (CreatedObject != nullptr) DestroyObject(CreatedObject);
            Rollback();
            return false;
        }
        Created.push_back({&Record, static_cast<PActorComponent*>(CreatedObject)});
    }

    for (const FCreatedSubobject& Entry : Created)
    {
        PObject* AttachParent = Entry.Record->AttachParentName.IsNone()
            ? nullptr : FindObject(this, Entry.Record->AttachParentName);
        if ((!Entry.Record->AttachParentName.IsNone() && AttachParent == nullptr)
            || !OnDefaultSubobjectRelation(
                Entry.Component,
                AttachParent,
                Entry.Record->AttachSocketName,
                Entry.Record->bIsRoot))
        {
            Rollback();
            return false;
        }
    }

    if (HasBegunPlay())
    {
        for (const FCreatedSubobject& Entry : Created)
            Entry.Component->RegisterComponent();
    }
    if (OutAddedCount != nullptr) *OutAddedCount = Created.size();
    return true;
}

PSceneComponent* PActor::GetRootComponent() const
{
    PActorComponent* Component = ResolveComponent(RootComponentHandle);
    return Component != nullptr && Component->IsA(PSceneComponent::StaticClass())
        ? static_cast<PSceneComponent*>(Component)
        : nullptr;
}

bool PActor::SetRootComponent(PSceneComponent* Component)
{
    if (Component == nullptr
        || Component->IsBeginningDestroy()
        || !OwnsComponent(Component))
    {
        return false;
    }

    if (Component->GetAttachParent() != nullptr
        && !Component->DetachFromComponent(EAttachmentTransformRule::KeepWorld))
    {
        return false;
    }

    RootComponentHandle = Component->GetHandle();
    return true;
}

FTransform PActor::GetActorTransform() const
{
    const PSceneComponent* RootComponent = GetRootComponent();
    return RootComponent != nullptr
        ? RootComponent->GetWorldTransform()
        : FTransform::Identity;
}

bool PActor::SetActorTransform(const FTransform& Transform)
{
    PSceneComponent* RootComponent = GetRootComponent();
    if (RootComponent == nullptr)
    {
        return false;
    }

    RootComponent->SetWorldTransform(Transform);
    RecordReplicationDirty(0, true);
    return true;
}

FVector3 PActor::GetActorLocation() const
{
    return GetActorTransform().Translation;
}

bool PActor::SetActorLocation(const FVector3& Location)
{
    FTransform Transform = GetActorTransform();
    Transform.Translation = Location;
    return SetActorTransform(Transform);
}

FRotator PActor::GetActorRotation() const
{
    return GetActorTransform().Rotation.Rotator();
}

bool PActor::SetActorRotation(const FRotator& Rotation)
{
    FTransform Transform = GetActorTransform();
    Transform.Rotation = Rotation.Quaternion();
    return SetActorTransform(Transform);
}

FVector3 PActor::GetActorScale() const
{
    return GetActorTransform().Scale;
}

bool PActor::SetActorScale(const FVector3& Scale)
{
    FTransform Transform = GetActorTransform();
    Transform.Scale = Scale;
    return SetActorTransform(Transform);
}

void PActor::BeginPlay()
{
}

void PActor::Tick(float)
{
}

void PActor::EndPlay()
{
}

void PActor::BeginDestroy()
{
    DispatchEndPlay();
    DispatchDestroyed();
    ComponentHandles.clear();
    RootComponentHandle = {};
    PObject::BeginDestroy();
}

bool PActor::OnDefaultSubobjectCreated(PObject* Subobject)
{
    if (Subobject == nullptr
        || !Subobject->IsA(PActorComponent::StaticClass())
        || Subobject->GetOuter() != this)
    {
        return false;
    }

    PActorComponent* Component = static_cast<PActorComponent*>(Subobject);
    if (!OwnsComponent(Component))
    {
        ComponentHandles.push_back(Component->GetHandle());
    }
    return true;
}

bool PActor::OnDefaultSubobjectRelation(
    PObject* Subobject,
    PObject* AttachParent,
    FName AttachSocketName,
    bool bIsRoot)
{
    if (Subobject == nullptr || !Subobject->IsA(PActorComponent::StaticClass()))
    {
        return false;
    }

    if (bIsRoot)
    {
        return AttachParent == nullptr
            && Subobject->IsA(PSceneComponent::StaticClass())
            && SetRootComponent(static_cast<PSceneComponent*>(Subobject));
    }
    if (AttachParent == nullptr)
    {
        return true;
    }
    if (!Subobject->IsA(PSceneComponent::StaticClass())
        || !AttachParent->IsA(PSceneComponent::StaticClass()))
    {
        return false;
    }
    return static_cast<PSceneComponent*>(Subobject)->AttachToComponent(
        static_cast<PSceneComponent*>(AttachParent),
        EAttachmentTransformRule::KeepRelative,
        AttachSocketName);
}

void PActor::DispatchBeginPlay()
{
    if (!bHasBegunPlay && !bPendingDestroy)
    {
        bHasBegunPlay = true;
        if (PWorld* World = GetWorld())
        {
            World->GetTickTaskManager().RegisterTickFunction(PrimaryActorTick, this);
        }
        RegisterAllComponents();
        BeginPlay();
    }
}

void PActor::DispatchTick(float DeltaSeconds)
{
    if (bHasBegunPlay && !bPendingDestroy)
    {
        Tick(DeltaSeconds);
    }
}

void PActor::DispatchEndPlay()
{
    if (bHasBegunPlay && !bHasEndedPlay)
    {
        bHasEndedPlay = true;
        if (PWorld* World = GetWorld())
        {
            World->GetTickTaskManager().UnregisterTickFunction(PrimaryActorTick);
        }
        EndPlay();
        UnregisterAllComponents();
    }
}

void PActor::DispatchDestroyed()
{
    if (bDestroyedEventBroadcast)
    {
        return;
    }

    bDestroyedEventBroadcast = true;
    try
    {
        ActorDestroyedEvent.Broadcast(this);
    }
    catch (const std::exception& Exception)
    {
        PICO_LOG(
            LogEngine,
            Error,
            "OnDestroyed listener for '{}' threw an exception: {}",
            GetPathName(),
            Exception.what());
    }
    catch (...)
    {
        PICO_LOG(
            LogEngine,
            Error,
            "OnDestroyed listener for '{}' threw an unknown exception",
            GetPathName());
    }
}

void PActor::MarkPendingDestroy()
{
    bPendingDestroy = true;
}

void PActor::SetOwner(PActor* InOwner)
{
    OwnerHandle = InOwner != nullptr ? InOwner->GetHandle() : FObjectHandle {};
}

void PActor::SetNetObjectId(FNetObjectId InNetObjectId)
{
    NetObjectId = InNetObjectId;
}

void PActor::SetNetRoles(ENetRole InLocalRole, ENetRole InRemoteRole)
{
    LocalRole = InLocalRole;
    RemoteRole = InRemoteRole;
}

PActorComponent* PActor::ResolveComponent(FObjectHandle Handle) const
{
    PObject* Object = ResolveObject(Handle);
    return Object != nullptr && Object->IsA(PActorComponent::StaticClass())
        ? static_cast<PActorComponent*>(Object)
        : nullptr;
}

bool PActor::OwnsComponent(const PActorComponent* Component) const
{
    if (Component == nullptr || Component->GetOwner() != this)
    {
        return false;
    }

    return std::any_of(
        ComponentHandles.begin(),
        ComponentHandles.end(),
        [this, Component](FObjectHandle Handle)
        {
            return ResolveComponent(Handle) == Component;
        });
}

void PActor::RegisterAllComponents()
{
    const std::vector<PActorComponent*> Components = GetComponents();
    for (PActorComponent* Component : Components)
    {
        if (Component != nullptr)
        {
            Component->RegisterComponent();
        }
    }
}

void PActor::UnregisterAllComponents()
{
    const std::vector<PActorComponent*> Components = GetComponents();
    for (PActorComponent* Component : Components)
    {
        if (Component != nullptr)
        {
            Component->UnregisterComponent();
        }
    }
}
}
