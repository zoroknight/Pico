#include "Pico/Engine/Controller.h"

#include "Pico/Engine/Pawn.h"

namespace Pico
{
PICO_DEFINE_CLASS(PController)

bool PController::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::Replicated;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Pawn, Metadata);
    return Class.AddProperties(std::move(Properties));
}

PController::PController(const FObjectConstructionParams& Params)
    : PActor(Params)
{
    PrimaryActorTick.SetTickGroup(ETickGroup::PrePhysics);
}

PPawn* PController::GetPawn() const
{
    return Pawn.Get();
}

bool PController::Possess(PPawn* InPawn)
{
    if (InPawn == nullptr
        || InPawn->IsPendingDestroy()
        || InPawn->IsBeginningDestroy()
        || InPawn->GetWorld() != GetWorld()
        || IsPendingDestroy()
        || IsBeginningDestroy())
    {
        return false;
    }
    if (Pawn.Get() == InPawn && InPawn->GetController() == this)
    {
        return true;
    }
    if (PController* OldController = InPawn->GetController())
    {
        OldController->UnPossess();
    }
    UnPossess();
    if (!SetPawn(InPawn))
    {
        return false;
    }
    InPawn->PossessedBy(this);
    OnPossess(InPawn);
    PossessedPawnChangedEvent.Broadcast(nullptr, InPawn);
    return true;
}

void PController::UnPossess()
{
    PPawn* OldPawn = Pawn.Get();
    if (OldPawn == nullptr)
    {
        Pawn.Reset();
        return;
    }
    SetPawn(nullptr);
    OldPawn->UnPossessed();
    OnUnPossess(OldPawn);
    PossessedPawnChangedEvent.Broadcast(OldPawn, nullptr);
}

FOnPossessedPawnChanged& PController::OnPossessedPawnChanged()
{
    return PossessedPawnChangedEvent;
}

bool PController::SetPawn(PPawn* InPawn)
{
    if (InPawn != nullptr && InPawn->GetWorld() != GetWorld())
    {
        return false;
    }

    PPawn* OldPawn = Pawn.Get();
    if (OldPawn == InPawn)
    {
        return true;
    }
    if (OldPawn != nullptr && OldPawn->GetController() == this)
    {
        OldPawn->SetController(nullptr);
    }
    Pawn = InPawn;
    if (InPawn != nullptr)
    {
        if (PController* OldController = InPawn->GetController())
        {
            OldController->Pawn.Reset();
        }
        InPawn->SetController(this);
    }
    return true;
}

void PController::OnPossess(PPawn*)
{
}

void PController::OnUnPossess(PPawn*)
{
}

void PController::BeginDestroy()
{
    UnPossess();
    PossessedPawnChangedEvent.Clear();
    PActor::BeginDestroy();
}
}
