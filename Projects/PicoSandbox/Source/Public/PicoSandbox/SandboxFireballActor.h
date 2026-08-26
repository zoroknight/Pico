#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxFireballActor.generated.h"

namespace PicoSandbox
{
class PSandboxPawn;

enum class ESandboxProjectileEffect : Pico::int32
{
    Gravity = 0,
    Burn = 1,
    Freeze = 2
};

PCLASS()
class PSandboxFireballActor final : public Pico::PActor
{
    GENERATED_BODY()

public:
    bool InitializeProjectile(
        PSandboxPawn* Source,
        PSandboxPawn* Target,
        ESandboxProjectileEffect Effect,
        float ProjectileSpeed,
        const Pico::FVector3& ProjectileColor);
    void Tick(float DeltaSeconds) override;

protected:
    explicit PSandboxFireballActor(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;

private:
    void ApplyVisualState();

    PFUNCTION()
    void OnRep_EffectType();

    PPROPERTY(Replicated, Transient, NotSerializable, InitialOnly, RepNotify=OnRep_EffectType)
    Pico::int32 EffectTypeValue =
        static_cast<Pico::int32>(ESandboxProjectileEffect::Burn);

    PPROPERTY(Replicated, Transient, NotSerializable, InitialOnly, RepNotify=OnRep_EffectType)
    Pico::FVector3 ProjectileColor = Pico::FVector3(1.0f, 0.12f, 0.04f);

    Pico::TWeakObjectPtr<PSandboxPawn> SourcePawn;
    Pico::TWeakObjectPtr<PSandboxPawn> TargetPawn;
    float RemainingLifetime = 3.0f;
    float Speed = 650.0f;
};
}
