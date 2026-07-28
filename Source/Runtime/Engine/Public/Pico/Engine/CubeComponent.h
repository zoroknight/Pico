#pragma once

#include "Pico/Engine/PrimitiveComponent.h"

namespace Pico
{
class PCubeComponent final : public PPrimitiveComponent
{
    PICO_DECLARE_CLASS(PCubeComponent, PPrimitiveComponent)

public:
    const FVector3& GetExtent() const;
    void SetExtent(const FVector3& InExtent);

protected:
    explicit PCubeComponent(const FObjectConstructionParams& Params);

private:
    FVector3 Extent = FVector3(50.0f);
};
}
