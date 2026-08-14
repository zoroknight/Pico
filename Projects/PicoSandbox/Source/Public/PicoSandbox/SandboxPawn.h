#pragma once

#include "Pico/Engine/Character.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPawn.generated.h"

namespace PicoSandbox
{
enum class EMovementReference : Pico::int32
{
    ControlRotation = 0,
    ActorRotation = 1,
    World = 2
};

const char* ToString(EMovementReference Reference);

PCLASS()
class PSandboxPawn final : public Pico::PCharacter
{
    GENERATED_BODY()

public:
    EMovementReference GetMovementReference() const;
    void SetMovementReference(EMovementReference Reference);
protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;
    void PostLoad() override;

private:
    PPROPERTY()
    Pico::int32 MovementReferenceValue =
        static_cast<Pico::int32>(EMovementReference::ControlRotation);
};
}
