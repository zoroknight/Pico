#pragma once

PCLASS()
class PInvalidRpcReflectedType final : public Pico::PObject
{
    GENERATED_BODY()

public:
    PFUNCTION(Server, Client, Reliable)
    void InvalidDirection();
};
