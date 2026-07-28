#include "Pico/Engine/ActorComponent.h"

#include "Pico/Engine/Actor.h"

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PActorComponent)

PActorComponent::PActorComponent(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

PActor* PActorComponent::GetOwner() const
{
    PObject* Outer = GetOuter();
    return Outer != nullptr && Outer->IsA(PActor::StaticClass())
        ? static_cast<PActor*>(Outer)
        : nullptr;
}

PWorld* PActorComponent::GetWorld() const
{
    PActor* Owner = GetOwner();
    return Owner != nullptr ? Owner->GetWorld() : nullptr;
}

bool PActorComponent::IsRegistered() const
{
    return bRegistered;
}

void PActorComponent::RegisterComponent()
{
    if (!bRegistered)
    {
        bRegistered = true;
        OnRegister();
    }
}

void PActorComponent::UnregisterComponent()
{
    if (bRegistered)
    {
        OnUnregister();
        bRegistered = false;
    }
}

void PActorComponent::BeginDestroy()
{
    UnregisterComponent();
    PObject::BeginDestroy();
}

void PActorComponent::OnRegister()
{
}

void PActorComponent::OnUnregister()
{
}
}
