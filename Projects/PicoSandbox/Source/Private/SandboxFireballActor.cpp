#include "PicoSandbox/SandboxFireballActor.h"

#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectInitializer.h"
#include "PicoSandbox/SandboxPawn.h"

#include <algorithm>

namespace PicoSandbox
{
PSandboxFireballActor::PSandboxFireballActor(
    const Pico::FObjectConstructionParams& Params)
    : PActor(Params)
{
    SetReplicates(true);
    PrimaryActorTick.SetCanEverTick(true);
    PrimaryActorTick.SetTickEnabled(true);
}

bool PSandboxFireballActor::DefineDefaultSubobjects(
    Pico::FObjectInitializer& Initializer)
{
    Pico::PCubeComponent* Visual =
        Initializer.CreateDefaultSubobject<Pico::PCubeComponent>("FireballVisual");
    if (Visual == nullptr) return false;
    Visual->SetExtent({12.0f, 12.0f, 12.0f});
    Visual->SetColor({1.0f, 0.12f, 0.04f});
    Visual->SetCollisionEnabled(Pico::ECollisionEnabled::NoCollision);
    return Initializer.SetRootSubobject(Visual);
}

bool PSandboxFireballActor::InitializeProjectile(
    PSandboxPawn* Source,
    PSandboxPawn* Target,
    ESandboxProjectileEffect Effect,
    float ProjectileSpeed,
    const Pico::FVector3& InProjectileColor)
{
    if (GetLocalRole() != Pico::ENetRole::Authority
        || Source == nullptr || Target == nullptr || Source == Target)
        return false;
    SourcePawn = Source;
    TargetPawn = Target;
    EffectTypeValue = static_cast<Pico::int32>(Effect);
    Speed = std::max(1.0f, ProjectileSpeed);
    ProjectileColor = InProjectileColor;
    ApplyVisualState();
    return SetActorLocation(Source->GetActorLocation() + Pico::FVector3(0.0f, 0.0f, 80.0f));
}

void PSandboxFireballActor::Tick(float DeltaSeconds)
{
    PActor::Tick(DeltaSeconds);
    if (GetLocalRole() != Pico::ENetRole::Authority) return;
    RemainingLifetime -= std::max(0.0f, DeltaSeconds);
    PSandboxPawn* Source = SourcePawn.Get();
    PSandboxPawn* Target = TargetPawn.Get();
    if (RemainingLifetime <= 0.0f || Source == nullptr || Target == nullptr
        || Source->IsPendingDestroy() || Target->IsPendingDestroy())
    {
        Destroy();
        return;
    }
    const Pico::FVector3 ToTarget =
        Target->GetActorLocation() + Pico::FVector3(0.0f, 0.0f, 70.0f)
        - GetActorLocation();
    const float Distance = ToTarget.Size();
    const float Step = Speed * std::max(0.0f, DeltaSeconds);
    if (Distance <= std::max(28.0f, Step))
    {
        Target->ApplyAuthorityProjectileEffect(
            Source, static_cast<ESandboxProjectileEffect>(EffectTypeValue));
        Destroy();
        return;
    }
    SetActorLocation(GetActorLocation() + ToTarget.GetSafeNormal() * Step);
}

void PSandboxFireballActor::OnRep_EffectType()
{
    ApplyVisualState();
}

void PSandboxFireballActor::ApplyVisualState()
{
    Pico::PObject* Object = Pico::FindObject(this, Pico::FName("FireballVisual"));
    if (Object == nullptr || !Object->IsA(Pico::PCubeComponent::StaticClass())) return;
    static_cast<Pico::PCubeComponent*>(Object)->SetColor(ProjectileColor);
}
}
