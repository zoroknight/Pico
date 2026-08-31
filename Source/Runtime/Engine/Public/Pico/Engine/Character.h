#pragma once

#include "Pico/Engine/CharacterMovementComponent.h"
#include "Pico/Engine/Pawn.h"

namespace Pico
{
class PCapsuleComponent;
class FObjectInitializer;

class PCharacter : public PPawn
{
    PICO_DECLARE_CLASS(PCharacter, PPawn)

public:
    PCapsuleComponent* GetCapsuleComponent() const;
    PCharacterMovementComponent* GetCharacterMovement() const;
    PCharacterMovementComponent* GetMovementComponent() const override;
    void Jump();
    void StopJumping();
    bool IsJumpPressed() const;
    bool ConsumeJumpInput();

protected:
    explicit PCharacter(const FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(FObjectInitializer& Initializer) override;
    bool OnDefaultSubobjectCreated(PObject* Subobject) override;

private:
    bool bPressedJump = false;
};
}
