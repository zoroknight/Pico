#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Engine/MatchState.h"
#include "Pico/Object/ObjectDelegate.h"

#include <vector>

namespace Pico
{
class PPlayerState;

using FOnGameStateMatchStateChanged =
    TObjectMulticastDelegate<void(EMatchState, EMatchState)>;

class PGameStateBase : public PActor
{
    PICO_DECLARE_CLASS(PGameStateBase, PActor)

public:
    bool AddPlayerState(PPlayerState* PlayerState);
    bool RemovePlayerState(PPlayerState* PlayerState);
    std::vector<PPlayerState*> GetPlayerStates() const;
    EMatchState GetMatchState() const;
    bool IsMatchInProgress() const;
    float GetElapsedMatchTime() const;
    FOnGameStateMatchStateChanged& OnMatchStateChanged();
    void Tick(float DeltaSeconds) override;

protected:
    explicit PGameStateBase(const FObjectConstructionParams& Params);
    void AddReferencedObjects(FReferenceCollector& Collector) const override;
    void BeginDestroy() override;

private:
    friend class PGameModeBase;
    bool SetMatchState(EMatchState NewState);

    std::vector<FObjectHandle> PlayerStateHandles;
    int32 MatchStateValue = static_cast<int32>(EMatchState::EnteringMap);
    float ElapsedMatchTime = 0.0f;
    FOnGameStateMatchStateChanged MatchStateChangedEvent;
};
}
