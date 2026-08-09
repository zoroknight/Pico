#pragma once

#include "Pico/Object/ReflectionMacros.h"
#include "ValidReflectedType.generated.h"

namespace PicoTest
{
PCLASS()
class PValidReflectedType final : public Pico::PObject
{
    GENERATED_BODY()

public:
    PFUNCTION(Pure)
    int32 GetScore() const;

private:
    PPROPERTY(Replicated, ReadOnly)
    int32 Score = 42;
};
}
