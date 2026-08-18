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

protected:
    explicit PSandboxReplicationLabActor(
        const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;

private:
    void ApplyVisualState();

    PPROPERTY(Replicated, Transient, NotSerializable, InitialOnly)
    Pico::int32 InitialSpawnMarker = 202;

    PPROPERTY(Replicated, Transient, NotSerializable, RepNotify=OnRep_LabRevision)
    Pico::int32 LabRevision = 0;

    PFUNCTION()
    void OnRep_LabRevision();

    Pico::int32 RepNotifyCount = 0;
};
}
