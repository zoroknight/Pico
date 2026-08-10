#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ObjectPtr.h"

namespace Pico
{
class PPawn;

class PController : public PActor
{
    PICO_DECLARE_CLASS(PController, PActor)

public:
    PPawn* GetPawn() const;
    bool Possess(PPawn* InPawn);
    void UnPossess();

protected:
    explicit PController(const FObjectConstructionParams& Params);
    void BeginDestroy() override;
    bool SetPawn(PPawn* InPawn);
    virtual void OnPossess(PPawn* InPawn);
    virtual void OnUnPossess(PPawn* InPawn);

private:
    TWeakObjectPtr<PPawn> Pawn;
};
}
