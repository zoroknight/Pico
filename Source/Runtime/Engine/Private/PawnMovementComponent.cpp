#include "Pico/Engine/PawnMovementComponent.h"

#include "Pico/Engine/Controller.h"
#include "Pico/Engine/Pawn.h"

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PPawnMovementComponent)

PPawnMovementComponent::PPawnMovementComponent(
    const FObjectConstructionParams& Params)
    : PMovementComponent(Params)
{
}

PPawn* PPawnMovementComponent::GetPawnOwner() const
{
    PActor* Owner = GetOwner();
    return Owner != nullptr && Owner->IsA(PPawn::StaticClass())
        ? static_cast<PPawn*>(Owner)
        : nullptr;
}

FVector3 PPawnMovementComponent::ConsumeInputVector()
{
    PPawn* Pawn = GetPawnOwner();
    return Pawn != nullptr
        ? Pawn->ConsumeMovementInputVector()
        : FVector3::ZeroVector;
}

void PPawnMovementComponent::RefreshControllerTickPrerequisite()
{
    if (PController* Previous = TickPrerequisiteController.Get())
    {
        PrimaryComponentTick.RemovePrerequisite(Previous->PrimaryActorTick);
    }
    TickPrerequisiteController.Reset();

    PPawn* Pawn = GetPawnOwner();
    PController* Controller = Pawn != nullptr ? Pawn->GetController() : nullptr;
    if (Controller != nullptr
        && PrimaryComponentTick.IsRegistered()
        && Controller->PrimaryActorTick.IsRegistered()
        && PrimaryComponentTick.AddPrerequisite(Controller->PrimaryActorTick))
    {
        TickPrerequisiteController = Controller;
    }
}

void PPawnMovementComponent::OnRegister()
{
    PMovementComponent::OnRegister();
}

void PPawnMovementComponent::OnTickRegistered()
{
    PMovementComponent::OnTickRegistered();
    RefreshControllerTickPrerequisite();
}

void PPawnMovementComponent::OnUnregister()
{
    if (PController* Controller = TickPrerequisiteController.Get())
    {
        PrimaryComponentTick.RemovePrerequisite(Controller->PrimaryActorTick);
    }
    TickPrerequisiteController.Reset();
    PMovementComponent::OnUnregister();
}
}
