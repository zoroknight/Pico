#pragma once

#include "Pico/Engine/Controller.h"
#include "Pico/Object/ObjectPtr.h"

namespace Pico
{
class PPlayerState;
class PPlayer;
class PActor;

class PPlayerController : public PController
{
    PICO_DECLARE_CLASS(PPlayerController, PController)

public:
    PPlayerState* GetPlayerState() const;
    PPlayer* GetPlayer() const;
    PActor* GetViewTarget() const;
    bool SetViewTarget(PActor* InViewTarget);
    void ClearViewTarget();

protected:
    explicit PPlayerController(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    bool SetPlayerState(PPlayerState* InPlayerState);
    void OnPossess(PPawn* InPawn) override;
    void OnUnPossess(PPawn* InPawn) override;

private:
    friend class PGameModeBase;
    friend class PPlayer;
    void SetPlayer(PPlayer* InPlayer);

    TObjectPtr<PPlayerState> PlayerState;
    TWeakObjectPtr<PPlayer> Player;
    TWeakObjectPtr<PActor> ViewTarget;
};
}
