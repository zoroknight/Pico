#pragma once

#include "Pico/Engine/Actor.h"

namespace Pico
{
class PCameraComponent;

class PCameraActor final : public PActor
{
    PICO_DECLARE_CLASS(PCameraActor, PActor)

public:
    PCameraComponent* GetCameraComponent() const;

protected:
    explicit PCameraActor(const FObjectConstructionParams& Params);
    bool DefineDefaultSubobjects(FObjectInitializer& Initializer) override;
};
}
