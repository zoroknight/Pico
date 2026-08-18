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
    PrimaryActorTick.SetCanEverTick(false);
}

bool PSandboxReplicationLabActor::DefineDefaultSubobjects(
    Pico::FObjectInitializer& Initializer)
{
    Pico::PCubeComponent* Cube =
        Initializer.CreateDefaultSubobject<Pico::PCubeComponent>("ReplicationLabCube");
    if (Cube == nullptr)
    {
        return false;
    }
    Cube->SetExtent({55.0f, 55.0f, 55.0f});
    Cube->SetCollisionEnabled(Pico::ECollisionEnabled::NoCollision);
    Cube->SetColor({0.10f, 0.85f, 0.35f});
    return Initializer.SetRootSubobject(Cube);
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
    InitialSpawnMarker = Marker;
}

void PSandboxReplicationLabActor::AdvanceRevision()
{
    ++LabRevision;
    ApplyVisualState();
}

void PSandboxReplicationLabActor::OnRep_LabRevision()
{
    ++RepNotifyCount;
    ApplyVisualState();
}

void PSandboxReplicationLabActor::ApplyVisualState()
{
    Pico::PObject* Object = Pico::FindObject(
        this, Pico::FName("ReplicationLabCube"));
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
    static_cast<Pico::PCubeComponent*>(Object)->SetColor(
        Colors[static_cast<std::size_t>(LabRevision) % std::size(Colors)]);
}
}
