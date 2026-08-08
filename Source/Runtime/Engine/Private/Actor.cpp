#include "Pico/Engine/Actor.h"

#include "Pico/Core/Log.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/SceneComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/ObjectGlobals.h"

#include <algorithm>
#include <exception>
#include <functional>

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PActor)

PActor::PActor(const FObjectConstructionParams& Params)
    : PObject(Params)
{
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
