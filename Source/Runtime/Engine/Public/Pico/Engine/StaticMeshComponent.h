#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Engine/PrimitiveComponent.h"

namespace Pico
{
class PStaticMeshComponent final : public PPrimitiveComponent
{
    PICO_DECLARE_CLASS(PStaticMeshComponent, PPrimitiveComponent)

public:
    const FAssetPath& GetStaticMeshAsset() const;
    void SetStaticMeshAsset(const FAssetPath& InAssetPath);

protected:
    explicit PStaticMeshComponent(const FObjectConstructionParams& Params);

private:
    FAssetPath StaticMeshAsset;
};
}
