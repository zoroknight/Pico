#pragma once

#include "Pico/Engine/Pawn.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPawn.generated.h"

namespace PicoSandbox
{
PCLASS()
class PSandboxPawn final : public Pico::PPawn
{
    GENERATED_BODY()

public:
    float GetMoveSpeed() const;
    void MoveFromInput(float Forward, float Right, float DeltaSeconds);
    void MoveInWorldDirection(
        const Pico::FVector3& WorldDirection,
        float DeltaSeconds);

protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;

private:
    PPROPERTY()
    float MoveSpeed = 250.0f;
};
}
