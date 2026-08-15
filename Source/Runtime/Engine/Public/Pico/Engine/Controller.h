#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ObjectPtr.h"

namespace Pico
{
class PPawn;

using FOnPossessedPawnChanged = TObjectMulticastDelegate<void(PPawn*, PPawn*)>;

class PController : public PActor
{
    PICO_DECLARE_CLASS(PController, PActor)

public:
    PPawn* GetPawn() const;
    const FRotator& GetControlRotation() const;
    void SetControlRotation(const FRotator& Rotation);
    void AddYawInput(float Value);
    void AddPitchInput(float Value);
    float GetViewPitchMin() const;
    float GetViewPitchMax() const;
    void SetViewPitchLimits(float InMinPitch, float InMaxPitch);
    bool Possess(PPawn* InPawn);
    void UnPossess();
    FOnPossessedPawnChanged& OnPossessedPawnChanged();
    void BeginPlay() override;

protected:
    explicit PController(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    void PostLoad() override;
    void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
    bool SetPawn(PPawn* InPawn);
    virtual void OnPossess(PPawn* InPawn);
    virtual void OnUnPossess(PPawn* InPawn);

private:
    void SanitizeViewPitchLimits();

    TWeakObjectPtr<PPawn> Pawn;
    FRotator ControlRotation;
    float ViewPitchMin = -85.0f;
    float ViewPitchMax = 85.0f;
    FOnPossessedPawnChanged PossessedPawnChangedEvent;
};
}
