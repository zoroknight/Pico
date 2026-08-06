#pragma once

#include "DeleteAssetsDialog.h"
#include "MaterialDialog.h"
#include "StaticMeshImportDialog.h"

#include "Pico/Editor/EditorAssetSelection.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class FEditorAssetService;
class FEditorCommandService;
class FEngineLoop;

class FEditorAssetWorkflowController
{
public:
    using FStatus = std::function<void(std::string, bool)>;
    using FInvalidateStaticMesh = std::function<void(const FAssetPath&)>;

    FEditorAssetWorkflowController(
        FEngineLoop* EngineLoop,
        FEditorAssetService* AssetService,
        FEditorCommandService* CommandService,
        FEditorAssetSelection* AssetSelection,
        FStatus SetStatus,
        FInvalidateStaticMesh InvalidateStaticMesh);

    void Draw();
    void OpenImport();
    void OpenTextureImport();
    void OpenCreateMaterial();
    void OpenEditMaterial(const FAssetPath& AssetPath);
    void OpenReimportOptions(const FAssetPath& AssetPath);
    void OpenDelete(const std::vector<FAssetPath>& AssetPaths);
    void OpenRename(const FAssetPath& AssetPath);
    void RefreshRegistry();
    void Reimport(const FAssetPath& AssetPath);
    bool CanReimport(const FAssetPath& AssetPath) const;

private:
    void ConfirmImport(const FStaticMeshImportRequest& Request);
    void ConfirmDelete(const FDeleteAssetsRequest& Request);
    void ConfirmMaterial(const FMaterialSaveRequest& Request);
    void DrawRenameDialog();
    void ConfirmRename();
    FAssetPath FindUniqueAssetPath(
        std::string_view Folder,
        std::string_view BaseName,
        std::string_view Extension) const;
    FAssetPath GetSelectedTexture() const;
    std::vector<std::string> FindAssetReferences(
        const std::vector<FAssetPath>& AssetPaths) const;
    void Report(FEditorAssetResult Result);

    FEngineLoop* EngineLoop = nullptr;
    FEditorAssetService* AssetService = nullptr;
    FEditorCommandService* CommandService = nullptr;
    FEditorAssetSelection* AssetSelection = nullptr;
    FStatus SetStatus;
    FInvalidateStaticMesh InvalidateStaticMesh;
    FStaticMeshImportDialog ImportDialog;
    FDeleteAssetsDialog DeleteDialog;
    FMaterialDialog MaterialDialog;
    FAssetPath RenameAssetPath;
    std::array<char, 128> RenameBuffer {};
    bool bOpenRenamePopup = false;
};
}
