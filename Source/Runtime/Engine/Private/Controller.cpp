#include "Pico/Engine/Controller.h"

#include "Pico/Engine/Pawn.h"

#include <algorithm>
#include <cmath>

namespace Pico
{
PICO_DEFINE_CLASS(PController)

bool PController::RegisterProperties(PClass& Class)
{
    FPropertyMetadata Metadata;
    Metadata.Flags = EPropertyFlags::Transient | EPropertyFlags::Replicated;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Pawn, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, ControlRotation, Metadata);
    return Class.AddProperties(std::move(Properties));
}

const FRotator& PController::GetControlRotation() const { return ControlRotation; }

void PController::SetControlRotation(const FRotator& Rotation)
{
    ControlRotation = Rotation.GetNormalized();
    ControlRotation.Pitch = std::clamp(ControlRotation.Pitch, -85.0f, 85.0f);
    ControlRotation.Roll = 0.0f;
}

void PController::AddYawInput(float Value)
{
    if (std::isfinite(Value)) SetControlRotation(
        {ControlRotation.Pitch, ControlRotation.Yaw + Value, 0.0f});
}

void PController::AddPitchInput(float Value)
{
    if (std::isfinite(Value)) SetControlRotation(
        {ControlRotation.Pitch + Value, ControlRotation.Yaw, 0.0f});
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

void PController::BeginPlay()
{
    PActor::BeginPlay();
    if (PPawn* PossessedPawn = GetPawn())
    {
        PossessedPawn->RefreshMovementTickPrerequisites();
    }
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
        OldPawn->RefreshMovementTickPrerequisites();
    }
    Pawn = InPawn;
    if (InPawn != nullptr)
    {
        if (PController* OldController = InPawn->GetController())
        {
            OldController->Pawn.Reset();
        }
        InPawn->SetController(this);
        InPawn->RefreshMovementTickPrerequisites();
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
