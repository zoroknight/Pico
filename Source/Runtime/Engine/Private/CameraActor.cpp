#include "Pico/Engine/CameraActor.h"

#include "Pico/Engine/CameraComponent.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectInitializer.h"

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PCameraActor)

PCameraActor::PCameraActor(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    PrimaryActorTick.SetCanEverTick(false);
}

bool PCameraActor::DefineDefaultSubobjects(FObjectInitializer& Initializer)
{
    PCameraComponent* Camera =
        Initializer.CreateDefaultSubobject<PCameraComponent>("CameraComponent");
    return Camera != nullptr && Initializer.SetRootSubobject(Camera);
}

PCameraComponent* PCameraActor::GetCameraComponent() const
{
    PObject* Object = FindObject(const_cast<PCameraActor*>(this), FName("CameraComponent"));
    return Object != nullptr && Object->IsA(PCameraComponent::StaticClass())
        ? static_cast<PCameraComponent*>(Object) : nullptr;
}
}
