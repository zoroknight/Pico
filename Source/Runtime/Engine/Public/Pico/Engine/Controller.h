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
    bool Possess(PPawn* InPawn);
    void UnPossess();
    FOnPossessedPawnChanged& OnPossessedPawnChanged();
    void BeginPlay() override;

protected:
    explicit PController(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    bool SetPawn(PPawn* InPawn);
    virtual void OnPossess(PPawn* InPawn);
    virtual void OnUnPossess(PPawn* InPawn);

private:
    TWeakObjectPtr<PPawn> Pawn;
    FRotator ControlRotation;
    FOnPossessedPawnChanged PossessedPawnChangedEvent;
};
}
