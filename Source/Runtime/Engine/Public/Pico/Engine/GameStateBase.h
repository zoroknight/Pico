#pragma once

#include "Pico/Engine/Actor.h"

#include <vector>

namespace Pico
{
class PPlayerState;

class PGameStateBase : public PActor
{
    PICO_DECLARE_CLASS(PGameStateBase, PActor)

public:
    bool AddPlayerState(PPlayerState* PlayerState);
    bool RemovePlayerState(PPlayerState* PlayerState);
    std::vector<PPlayerState*> GetPlayerStates() const;

protected:
    explicit PGameStateBase(const FObjectConstructionParams& Params);
    void AddReferencedObjects(FReferenceCollector& Collector) const override;
    void BeginDestroy() override;

private:
    std::vector<FObjectHandle> PlayerStateHandles;
};
}
