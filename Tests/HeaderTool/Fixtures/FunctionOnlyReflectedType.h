#pragma once

#include "Pico/Object/ReflectionMacros.h"
#include "FunctionOnlyReflectedType.generated.h"

namespace PicoTest
{
PCLASS()
class PFunctionOnlyReflectedType final : public Pico::PObject
{
    GENERATED_BODY()

public:
    PFUNCTION(Callable)
    void Notify(int32 Value);
};
}
