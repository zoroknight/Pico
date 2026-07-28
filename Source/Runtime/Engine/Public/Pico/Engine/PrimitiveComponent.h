#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Engine/SceneComponent.h"

namespace Pico
{
class PPrimitiveComponent : public PSceneComponent
{
    PICO_DECLARE_CLASS(PPrimitiveComponent, PSceneComponent)

public:
    bool IsVisible() const;
    void SetVisible(bool bInVisible);

    const FVector3& GetColor() const;
    void SetColor(const FVector3& InColor);

protected:
    explicit PPrimitiveComponent(const FObjectConstructionParams& Params);

private:
    bool bVisible = true;
    FVector3 Color = FVector3(0.16f, 0.62f, 0.52f);
};
}
