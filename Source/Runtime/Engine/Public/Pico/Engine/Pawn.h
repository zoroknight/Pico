#pragma once

#include "Pico/Engine/Actor.h"
#include "Pico/Object/ObjectPtr.h"

namespace Pico
{
class PController;

class PPawn : public PActor
{
    PICO_DECLARE_CLASS(PPawn, PActor)

public:
    PController* GetController() const;
    virtual void PossessedBy(PController* NewController);
    virtual void UnPossessed();

protected:
    explicit PPawn(const FObjectConstructionParams& Params);
    void BeginDestroy() override;

private:
    friend class PController;
    void SetController(PController* InController);

    TWeakObjectPtr<PController> Controller;
};
}
