#pragma once

#include "Pico/Core/AssetPath.h"

#include <optional>
#include <string>
#include <vector>

namespace Pico
{
struct FDeleteAssetsRequest
{
    std::vector<FAssetPath> AssetPaths;
    bool bDeleteProjectSources = false;
    bool bClearSceneReferences = false;
};

class FDeleteAssetsDialog
{
public:
    void Open(
        std::vector<FAssetPath> AssetPaths,
        std::vector<std::string> SceneReferences);
    std::optional<FDeleteAssetsRequest> Draw();

private:
    std::vector<FAssetPath> AssetPaths;
    std::vector<std::string> SceneReferences;
    bool bDeleteProjectSources = false;
    bool bOpenPopup = false;
};
}
