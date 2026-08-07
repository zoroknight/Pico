#pragma once

#include "Pico/Engine/GameInstance.h"
#include "Pico/Object/ObjectTypes.h"

namespace PicoSandbox
{
class PSandboxPawn;

class FSandboxGameInstance final : public Pico::FGameInstance
{
public:
    bool Init(Pico::FGameEngine& GameEngine) override;
    void Shutdown() override;

    PSandboxPawn* GetPawn() const;

private:
    Pico::FObjectHandle PawnHandle;
};
}
