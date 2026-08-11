#pragma once

#include "Pico/Engine/MovementComponent.h"

namespace Pico
{
class PController;
class PPawn;

class PPawnMovementComponent : public PMovementComponent
{
    PICO_DECLARE_CLASS(PPawnMovementComponent, PMovementComponent)

public:
    PPawn* GetPawnOwner() const;
    FVector3 ConsumeInputVector();
    void RefreshControllerTickPrerequisite();

protected:
    explicit PPawnMovementComponent(const FObjectConstructionParams& Params);
    void OnRegister() override;
    void OnTickRegistered() override;
    void OnUnregister() override;

private:
    TWeakObjectPtr<PController> TickPrerequisiteController;
};
}
