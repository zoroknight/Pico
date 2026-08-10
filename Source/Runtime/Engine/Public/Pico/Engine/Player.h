#pragma once

#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/ReflectionMacros.h"

namespace Pico
{
class PGameInstance;
class PGameModeBase;
class PPlayerController;

class PPlayer : public PObject
{
    PICO_DECLARE_CLASS(PPlayer, PObject)

public:
    PPlayerController* GetPlayerController() const;
    PGameInstance* GetGameInstance() const;

protected:
    explicit PPlayer(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

private:
    friend class PGameInstance;
    friend class PGameModeBase;
    friend class PPlayerController;
    void SetPlayerController(PPlayerController* InPlayerController);

    TWeakObjectPtr<PPlayerController> PlayerController;
};
}
