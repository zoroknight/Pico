#pragma once

#include "Pico/Engine/Actor.h"

namespace Pico
{
class PPlayerState : public PActor
{
    PICO_DECLARE_CLASS(PPlayerState, PActor)

public:
    int32 GetPlayerId() const;
    void SetPlayerId(int32 InPlayerId);
    float GetScore() const;
    void SetScore(float InScore);
    bool IsSpectator() const;
    void SetIsSpectator(bool bValue);

protected:
    explicit PPlayerState(const FObjectConstructionParams& Params);

private:
    int32 PlayerId = -1;
    float Score = 0.0f;
    bool bIsSpectator = false;
};
}
