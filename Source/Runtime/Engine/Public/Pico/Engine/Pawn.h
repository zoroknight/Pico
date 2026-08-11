#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ObjectPtr.h"

namespace Pico
{
class PController;
class PPawnMovementComponent;

class PPawn : public PActor
{
    PICO_DECLARE_CLASS(PPawn, PActor)

public:
    PController* GetController() const;
    virtual PPawnMovementComponent* GetMovementComponent() const;
    void AddMovementInput(
        const FVector3& WorldDirection,
        float ScaleValue = 1.0f,
        bool bForce = false);
    const FVector3& GetPendingMovementInputVector() const;
    const FVector3& GetLastMovementInputVector() const;
    FVector3 ConsumeMovementInputVector();
    virtual void PossessedBy(PController* NewController);
    virtual void UnPossessed();

protected:
    explicit PPawn(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

private:
    friend class PController;
    void SetController(PController* InController);
    void RefreshMovementTickPrerequisites();

    TWeakObjectPtr<PController> Controller;
    FVector3 PendingMovementInputVector = FVector3::ZeroVector;
    FVector3 LastMovementInputVector = FVector3::ZeroVector;
};
}
