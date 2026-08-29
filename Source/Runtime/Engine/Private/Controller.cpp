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
    Metadata.RepNotifyFunction = FName("OnRep_Pawn");
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Pawn, Metadata);
    Metadata.RepNotifyFunction = {};
    // Local view state stays responsive; authority-facing rotation travels through explicit moves/RPCs.
    Metadata.Flags = EPropertyFlags::Transient;
    PICO_ADD_PROPERTY_METADATA(Properties, ControlRotation, Metadata);
    Metadata.Flags = EPropertyFlags::None;
    PICO_ADD_PROPERTY_METADATA(Properties, ViewPitchMin, Metadata);
    PICO_ADD_PROPERTY_METADATA(Properties, ViewPitchMax, Metadata);
    if (!Class.AddProperties(std::move(Properties))) return false;
    std::vector<PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions, OnRep_Pawn, EFunctionFlags::Callable);
    return Class.AddFunctions(std::move(Functions));
}

const FRotator& PController::GetControlRotation() const { return ControlRotation; }

void PController::SetControlRotation(const FRotator& Rotation)
{
    ControlRotation = Rotation.GetNormalized();
    ControlRotation.Pitch = std::clamp(
        ControlRotation.Pitch, ViewPitchMin, ViewPitchMax);
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

float PController::GetViewPitchMin() const { return ViewPitchMin; }
float PController::GetViewPitchMax() const { return ViewPitchMax; }

void PController::SetViewPitchLimits(float InMinPitch, float InMaxPitch)
{
    ViewPitchMin = InMinPitch;
    ViewPitchMax = InMaxPitch;
    SanitizeViewPitchLimits();
    SetControlRotation(ControlRotation);
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

void PController::OnRep_Pawn()
{
    PPawn* ReplicatedPawn = Pawn.Get();
    if (ReplicatedPawn != nullptr)
    {
        Possess(ReplicatedPawn);
    }
    else
    {
        UnPossess();
    }
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
        if (LifecyclePawn.Get() != InPawn)
        {
            LifecyclePawn = InPawn;
            InPawn->PossessedBy(this);
            OnPossess(InPawn);
            PossessedPawnChangedEvent.Broadcast(nullptr, InPawn);
        }
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
    LifecyclePawn = InPawn;
    OnPossess(InPawn);
    PossessedPawnChangedEvent.Broadcast(nullptr, InPawn);
    return true;
}

void PController::UnPossess()
{
    PPawn* OldPawn = Pawn.Get();
    if (OldPawn == nullptr)
    {
        OldPawn = LifecyclePawn.Get();
    }
    if (OldPawn == nullptr)
    {
        Pawn.Reset();
        LifecyclePawn.Reset();
        return;
    }
    const bool bLifecycleWasActive = LifecyclePawn.Get() == OldPawn;
    if (Pawn.Get() != nullptr)
    {
        SetPawn(nullptr);
    }
    else if (OldPawn->GetController() == this)
    {
        OldPawn->SetController(nullptr);
        OldPawn->RefreshMovementTickPrerequisites();
    }
    LifecyclePawn.Reset();
    if (bLifecycleWasActive)
    {
        OldPawn->UnPossessed();
        OnUnPossess(OldPawn);
        PossessedPawnChangedEvent.Broadcast(OldPawn, nullptr);
    }
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

void PController::PostLoad()
{
    PActor::PostLoad();
    SanitizeViewPitchLimits();
    SetControlRotation(ControlRotation);
}

void PController::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    PActor::PostEditChangeProperty(Event);
    SanitizeViewPitchLimits();
    SetControlRotation(ControlRotation);
}

void PController::SanitizeViewPitchLimits()
{
    if (!std::isfinite(ViewPitchMin)) ViewPitchMin = -85.0f;
    if (!std::isfinite(ViewPitchMax)) ViewPitchMax = 85.0f;
    ViewPitchMin = std::clamp(ViewPitchMin, -89.9f, 89.9f);
    ViewPitchMax = std::clamp(ViewPitchMax, -89.9f, 89.9f);
    if (ViewPitchMin > ViewPitchMax) std::swap(ViewPitchMin, ViewPitchMax);
}
}
