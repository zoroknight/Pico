#pragma once

#include "Pico/AssetImport/StaticMeshImporter.h"
#include "Pico/AssetImport/TextureImporter.h"
#include "Pico/Asset/Material.h"
#include "Pico/Core/AssetPath.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Pico
{
class FEngineLoop;

struct FEditorAssetResult
{
    bool bSucceeded = false;
    std::string Message;
    FAssetPath AssetPath;
};

struct FStaticMeshSourceAnalysis
{
    FStaticMeshBounds Bounds;
    std::size_t VertexCount = 0;
    std::size_t TriangleCount = 0;
};

struct FStagedAssetDeletionFile
{
    std::filesystem::path Original;
    std::filesystem::path Temporary;
};

struct FStagedAssetDeletion
{
    std::vector<FAssetPath> AssetPaths;
    std::vector<FStagedAssetDeletionFile> Files;
    struct FMaterialReferenceEdit
    {
        FAssetPath AssetPath;
        std::filesystem::path FilePath;
        FMaterialData Before;
    };
    std::vector<FMaterialReferenceEdit> MaterialReferenceEdits;
    bool bDeleteProjectSources = false;
    bool bActive = false;
};

class FEditorAssetService
{
public:
    explicit FEditorAssetService(FEngineLoop* EngineLoop);

    FEditorAssetResult RefreshRegistry();
    FEditorAssetResult AnalyzeStaticMeshSource(
        const std::filesystem::path& SourceFile,
        FStaticMeshSourceAnalysis& OutAnalysis) const;
    FEditorAssetResult ImportStaticMesh(
        const std::filesystem::path& SourceFile,
        const FAssetPath& Destination,
        const FStaticMeshImportOptions& Options = {});
    FEditorAssetResult ReimportStaticMesh(FAssetPath AssetPath);
    FEditorAssetResult ReimportStaticMeshWithOptions(
        FAssetPath AssetPath,
        const FStaticMeshImportOptions& Options);
    FEditorAssetResult ImportTexture(
        const std::filesystem::path& SourceFile,
        const FAssetPath& Destination);
    FEditorAssetResult ReimportTexture(FAssetPath AssetPath);
    FEditorAssetResult CreateMaterial(
        const FAssetPath& Destination,
        const FMaterialData& Material = {});
    FEditorAssetResult SaveMaterial(
        FAssetPath AssetPath,
        const FMaterialData& Material);
    FEditorAssetResult RenameAsset(
        FAssetPath AssetPath,
        std::string NewName);
    FEditorAssetResult DeleteStaticMesh(
        FAssetPath AssetPath,
        bool bDeleteProjectSource);
    FEditorAssetResult DeleteStaticMeshes(
        std::vector<FAssetPath> AssetPaths,
        bool bDeleteProjectSources);
    FEditorAssetResult StageDeleteStaticMeshes(
        std::vector<FAssetPath> AssetPaths,
        bool bDeleteProjectSources,
        FStagedAssetDeletion& OutDeletion);
    FEditorAssetResult StageDeleteAssets(
        std::vector<FAssetPath> AssetPaths,
        bool bDeleteProjectSources,
        FStagedAssetDeletion& OutDeletion);
    FEditorAssetResult ClearStagedAssetReferences(
        FStagedAssetDeletion& Deletion);
    FEditorAssetResult CommitStagedDelete(FStagedAssetDeletion& Deletion);
    FEditorAssetResult RollbackStagedDelete(FStagedAssetDeletion& Deletion);
    bool CanReimportStaticMesh(const FAssetPath& AssetPath) const;
    bool CanReimportTexture(const FAssetPath& AssetPath) const;
    bool GetStaticMeshImportSettings(
        const FAssetPath& AssetPath,
        std::filesystem::path& OutSourceFile,
        FStaticMeshImportOptions& OutOptions) const;

private:
    bool ResolveContentPath(
        const FAssetPath& AssetPath,
        std::filesystem::path& OutPath) const;
    bool LoadImportMetadata(
        const FAssetPath& AssetPath,
        FAssetPath& OutSource,
        FStaticMeshImportOptions& OutOptions) const;
    bool WriteImportMetadata(
        const std::filesystem::path& DestinationFile,
        const FAssetPath& SourceAsset,
        const FStaticMeshImportOptions& Options) const;
    bool LoadTextureImportMetadata(
        const FAssetPath& AssetPath,
        FAssetPath& OutSource) const;
    bool WriteTextureImportMetadata(
        const std::filesystem::path& DestinationFile,
        const FAssetPath& SourceAsset) const;
    FEditorAssetResult FinishImport(FAssetPath AssetPath, std::string Message);

    FEngineLoop* EngineLoop = nullptr;
};
}
