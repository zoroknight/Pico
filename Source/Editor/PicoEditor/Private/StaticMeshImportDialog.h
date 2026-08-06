#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Editor/EditorAssetService.h"

#include <filesystem>
#include <optional>

namespace Pico
{
struct FStaticMeshImportRequest
{
    std::filesystem::path SourceFile;
    FAssetPath ReimportAsset;
    FStaticMeshImportOptions Options;
};

class FStaticMeshImportDialog
{
public:
    void Open(
        std::filesystem::path SourceFile,
        FAssetPath ReimportAsset,
        const FStaticMeshSourceAnalysis& Analysis,
        const FStaticMeshImportOptions& Options,
        bool bUseAutomaticScale);
    std::optional<FStaticMeshImportRequest> Draw();

private:
    void ApplyScalePreset();

    std::filesystem::path SourceFile;
    FAssetPath ReimportAsset;
    FStaticMeshSourceAnalysis Analysis;
    FStaticMeshImportOptions Options;
    float NormalizeTargetSize = 100.0f;
    int ScalePreset = 0;
    bool bOpenPopup = false;
};
}
