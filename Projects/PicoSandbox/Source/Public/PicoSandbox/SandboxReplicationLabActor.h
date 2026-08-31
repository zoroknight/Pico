#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxReplicationLabActor.generated.h"

namespace PicoSandbox
{
PCLASS()
class PSandboxReplicationLabActor final : public Pico::PActor
{
    GENERATED_BODY()

public:
    Pico::int32 GetInitialSpawnMarker() const;
    Pico::int32 GetLabRevision() const;
    Pico::int32 GetRepNotifyCount() const;
    void SetAuthoritySpawnMarker(Pico::int32 Marker);
    void AdvanceRevision();
    bool IsDoorOpen() const;
    Pico::int32 GetDoorUseCount() const;
    Pico::int32 GetMulticastPulseCount() const;
    bool IsDoorCollisionEnabled() const;
    void SetDoorCollisionEnabled(bool bEnabled);
    bool ToggleDoor();

protected:
    explicit PSandboxReplicationLabActor(
        const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;
    void PostLoad() override;
    void BeginPlay() override;
    void PostEditChangeProperty(
        const Pico::FPropertyChangedEvent& Event) override;

private:
    void ApplyVisualState();

    PPROPERTY(Replicated, Transient, NotSerializable, InitialOnly)
    Pico::int32 InitialSpawnMarker = 6202;

    PPROPERTY(Replicated, Transient, NotSerializable, RepNotify=OnRep_LabRevision)
    Pico::int32 LabRevision = 0;

    PFUNCTION()
    void OnRep_LabRevision();

    PPROPERTY(Replicated, RepNotify=OnRep_DoorOpen)
    bool bDoorOpen = false;

    PFUNCTION()
    void OnRep_DoorOpen();

    PPROPERTY(Replicated, RepNotify=OnRep_DoorCollisionEnabled)
    bool bDoorCollisionEnabled = true;

    PFUNCTION()
    void OnRep_DoorCollisionEnabled();

    PFUNCTION(NetMulticast)
    void MulticastDoorPulse(Pico::int32 Revision);

    Pico::int32 RepNotifyCount = 0;
    Pico::int32 DoorUseCount = 0;
    Pico::int32 MulticastPulseCount = 0;
    Pico::FVector3 ClosedLocation = Pico::FVector3::ZeroVector;
    bool bHasClosedLocation = false;
};
}
