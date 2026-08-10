#pragma once

#include "Pico/Engine/Actor.h"

namespace Pico
{
class PPlayerStart : public PActor
{
    PICO_DECLARE_CLASS(PPlayerStart, PActor)

public:
    int32 GetPlayerStartId() const;
    void SetPlayerStartId(int32 InId);

protected:
    explicit PPlayerStart(const FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(FObjectInitializer& Initializer) override;

private:
    int32 PlayerStartId = 0;
};
}
