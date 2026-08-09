#pragma once

PCLASS()
class PInvalidReflectedType final : public Pico::PObject
{
    PPROPERTY(DefinitelyNotAFlag)
    int32 Value = 0;
};
