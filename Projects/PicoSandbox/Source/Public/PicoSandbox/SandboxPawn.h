#pragma once

#include "Pico/Engine/Character.h"
#include "Pico/Object/ReflectionMacros.h"
#include "PicoSandbox/SandboxPawn.generated.h"

namespace PicoSandbox
{
PCLASS()
class PSandboxPawn final : public Pico::PCharacter
{
    GENERATED_BODY()

public:
protected:
    explicit PSandboxPawn(const Pico::FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(Pico::FObjectInitializer& Initializer) override;

};
}
