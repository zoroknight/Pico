#pragma once

#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Core/AssetPath.h"
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

    PFUNCTION()
    void OnRep_Score();

    PFUNCTION(Server, Reliable)
    void ServerSetScore(int32 NewScore);

private:
    PPROPERTY(Asset=ThirdPersonControlProfile)
    Pico::FAssetPath ControlProfile;

    PPROPERTY(Replicated, ReadOnly, InitialOnly, RepNotify=OnRep_Score)
    int32 Score = 42;
};
}
