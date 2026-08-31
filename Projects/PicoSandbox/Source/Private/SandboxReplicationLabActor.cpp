#include "PicoSandbox/SandboxReplicationLabActor.h"

#include "Pico/Engine/CubeComponent.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectInitializer.h"

namespace PicoSandbox
{
PSandboxReplicationLabActor::PSandboxReplicationLabActor(
    const Pico::FObjectConstructionParams& Params)
    : PActor(Params)
{
    SetReplicates(true);
    SetReplicateMovement(false);
    PrimaryActorTick.SetCanEverTick(false);
}

bool PSandboxReplicationLabActor::DefineDefaultSubobjects(
    Pico::FObjectInitializer& Initializer)
{
    Pico::PCubeComponent* Cube =
        Initializer.CreateDefaultSubobject<Pico::PCubeComponent>("DoorPanel");
    if (Cube == nullptr)
    {
        return false;
    }
    Cube->SetExtent({35.0f, 110.0f, 120.0f});
    Cube->SetCollisionEnabled(Pico::ECollisionEnabled::QueryAndPhysics);
    Cube->SetPhysicsBodyType(Pico::EPhysicsBodyType::Static);
    Cube->SetColor({0.10f, 0.85f, 0.35f});
    return Initializer.SetRootSubobject(Cube);
}

void PSandboxReplicationLabActor::PostLoad()
{
    PActor::PostLoad();
    ClosedLocation = GetActorLocation()
        - (bDoorOpen ? Pico::FVector3(0.0f, 220.0f, 0.0f)
                     : Pico::FVector3::ZeroVector);
    bHasClosedLocation = true;
    ApplyVisualState();
}

void PSandboxReplicationLabActor::BeginPlay()
{
    if (!bHasClosedLocation)
    {
        ClosedLocation = GetActorLocation()
            - (bDoorOpen ? Pico::FVector3(0.0f, 220.0f, 0.0f)
                         : Pico::FVector3::ZeroVector);
        bHasClosedLocation = true;
    }
    ApplyVisualState();
    PActor::BeginPlay();
}

void PSandboxReplicationLabActor::PostEditChangeProperty(
    const Pico::FPropertyChangedEvent& Event)
{
    PActor::PostEditChangeProperty(Event);
    if (Event.Property == nullptr) return;
    const Pico::FName Name = Event.Property->GetName();
    if (Name == Pico::FName("bDoorOpen")
        || Name == Pico::FName("bDoorCollisionEnabled"))
    {
        if (!bHasClosedLocation)
        {
            ClosedLocation = GetActorLocation();
            bHasClosedLocation = true;
        }
        ApplyVisualState();
    }
}

Pico::int32 PSandboxReplicationLabActor::GetInitialSpawnMarker() const
{
    return InitialSpawnMarker;
}

Pico::int32 PSandboxReplicationLabActor::GetLabRevision() const
{
    return LabRevision;
}

Pico::int32 PSandboxReplicationLabActor::GetRepNotifyCount() const
{
    return RepNotifyCount;
}

void PSandboxReplicationLabActor::SetAuthoritySpawnMarker(Pico::int32 Marker)
{
    if (InitialSpawnMarker == Marker) return;
    InitialSpawnMarker = Marker;
    MarkReplicatedPropertyDirty(Pico::FName("InitialSpawnMarker"));
}

void PSandboxReplicationLabActor::AdvanceRevision()
{
    ++LabRevision;
    MarkReplicatedPropertyDirty(Pico::FName("LabRevision"));
    ApplyVisualState();
}

bool PSandboxReplicationLabActor::IsDoorOpen() const { return bDoorOpen; }
Pico::int32 PSandboxReplicationLabActor::GetDoorUseCount() const
{ return DoorUseCount; }
Pico::int32 PSandboxReplicationLabActor::GetMulticastPulseCount() const
{ return MulticastPulseCount; }
bool PSandboxReplicationLabActor::IsDoorCollisionEnabled() const
{ return bDoorCollisionEnabled; }

void PSandboxReplicationLabActor::SetDoorCollisionEnabled(bool bEnabled)
{
    if (GetLocalRole() != Pico::ENetRole::Authority
        || bDoorCollisionEnabled == bEnabled) return;
    bDoorCollisionEnabled = bEnabled;
    MarkReplicatedPropertyDirty(Pico::FName("bDoorCollisionEnabled"));
    ApplyVisualState();
}

bool PSandboxReplicationLabActor::ToggleDoor()
{
    if (GetLocalRole() != Pico::ENetRole::Authority) return false;
    bDoorOpen = !bDoorOpen;
    MarkReplicatedPropertyDirty(Pico::FName("bDoorOpen"));
    ++DoorUseCount;
    ++LabRevision;
    MarkReplicatedPropertyDirty(Pico::FName("LabRevision"));
    if (!bHasClosedLocation)
    {
        ClosedLocation = GetActorLocation();
        bHasClosedLocation = true;
    }
    SetActorLocation(ClosedLocation
        + (bDoorOpen ? Pico::FVector3(0.0f, 220.0f, 0.0f)
                     : Pico::FVector3::ZeroVector));
    ApplyVisualState();
    return true;
}

void PSandboxReplicationLabActor::OnRep_LabRevision()
{
    ++RepNotifyCount;
    ApplyVisualState();
}

void PSandboxReplicationLabActor::OnRep_DoorOpen()
{
    ApplyVisualState();
}

void PSandboxReplicationLabActor::OnRep_DoorCollisionEnabled()
{
    ApplyVisualState();
}

void PSandboxReplicationLabActor::MulticastDoorPulse(Pico::int32 Revision)
{
    MulticastPulseCount = Revision;
}

void PSandboxReplicationLabActor::ApplyVisualState()
{
    Pico::PObject* Object = Pico::FindObject(
        this, Pico::FName("DoorPanel"));
    if (Object == nullptr || !Object->IsA(Pico::PCubeComponent::StaticClass()))
    {
        return;
    }
    static constexpr Pico::FVector3 Colors[] = {
        {0.10f, 0.85f, 0.35f},
        {1.00f, 0.72f, 0.12f},
        {0.20f, 0.62f, 1.00f},
        {0.95f, 0.22f, 0.55f}
    };
    auto* DoorPanel = static_cast<Pico::PCubeComponent*>(Object);
    DoorPanel->SetCollisionEnabled(
        !bDoorOpen && bDoorCollisionEnabled
            ? Pico::ECollisionEnabled::QueryAndPhysics
            : Pico::ECollisionEnabled::NoCollision);
    DoorPanel->SetColor(
        bDoorOpen
            ? Pico::FVector3(0.15f, 0.70f, 1.0f)
            : Colors[static_cast<std::size_t>(LabRevision) % std::size(Colors)]);
}
}
