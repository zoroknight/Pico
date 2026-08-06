#include "Pico/Editor/EditorAssetService.h"
#include "Pico/Editor/AssetDependencyService.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/EngineLoop.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
FEditorAssetResult Failure(std::string Message)
{
    return {false, std::move(Message), {}};
}

bool HasExtension(const FAssetPath& Path, std::string_view Extension)
{
    std::string Actual(Path.GetExtension());
    std::transform(Actual.begin(), Actual.end(), Actual.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    return Actual == Extension;
}

bool IsValidAssetName(std::string_view Name)
{
    return !Name.empty()
        && std::all_of(
            Name.begin(),
            Name.end(),
            [](unsigned char Character)
            {
                return std::isalnum(Character) || Character == '_';
            });
}
}

FEditorAssetService::FEditorAssetService(FEngineLoop* InEngineLoop)
    : EngineLoop(InEngineLoop)
{
}

FEditorAssetResult FEditorAssetService::RefreshRegistry()
{
    if (EngineLoop == nullptr)
    {
        return Failure("Asset service has no EngineLoop");
    }
    FAssetScanReport Report;
    if (!EngineLoop->GetAssetRegistry().ScanProjectContent(&Report))
    {
        return Failure("Could not scan project Content");
    }
    return {true,
        "Found " + std::to_string(Report.RegisteredAssetCount) + " asset(s)", {}};
}

FEditorAssetResult FEditorAssetService::AnalyzeStaticMeshSource(
    const std::filesystem::path& SourceFile,
    FStaticMeshSourceAnalysis& OutAnalysis) const
{
    OutAnalysis = {};
    FStaticMeshImportOptions Options;
    Options.UniformScale = 1.0f;
    Options.bConvertYUpToZUp = false;
    FStaticMeshImportResult ImportResult;
    EStaticMeshImportError Error = EStaticMeshImportError::None;
    if (!ImportObjStaticMesh(SourceFile, Options, ImportResult, &Error))
    {
        return Failure("Could not analyze OBJ: " + std::string(ToString(Error)));
    }
    OutAnalysis.Bounds = ImportResult.Mesh.Bounds;
    OutAnalysis.VertexCount = ImportResult.Mesh.Vertices.size();
    OutAnalysis.TriangleCount = ImportResult.Mesh.Indices.size() / 3;
    return {true, "Analyzed " + SourceFile.filename().string(), {}};
}

FEditorAssetResult FEditorAssetService::ImportStaticMesh(
    const std::filesystem::path& SourceFile,
    const FAssetPath& Destination,
    const FStaticMeshImportOptions& Options)
{
    if (EngineLoop == nullptr || !Destination.IsValid() || !HasExtension(Destination, ".pmesh"))
    {
        return Failure("Static Mesh destination must be a /Game path ending in .pmesh");
    }
    std::error_code Error;
    if (!std::filesystem::is_regular_file(SourceFile, Error))
    {
        return Failure("OBJ source file does not exist");
    }

    std::filesystem::path DestinationFile;
    if (!ResolveContentPath(Destination, DestinationFile))
    {
        return Failure("Could not resolve the destination inside project Content");
    }
    if (std::filesystem::exists(DestinationFile, Error))
    {
        return Failure("An asset already exists at " + std::string(Destination.ToString()));
    }

    std::filesystem::path SourceRelative = std::filesystem::path("Source")
        / std::filesystem::path(Destination.GetGameRelativePath());
    SourceRelative.replace_extension(".obj");
    std::filesystem::path ProjectSource;
    if (!FPaths::TryGetProjectWritePath(EProjectWriteRoot::Content, SourceRelative, ProjectSource))
    {
        return Failure("Could not create a project-local source path");
    }
    std::filesystem::create_directories(ProjectSource.parent_path(), Error);
    if (Error)
    {
        return Failure("Could not create the source directory");
    }
    const std::filesystem::path SourceTemp = ProjectSource.string() + ".tmp";
    std::filesystem::copy_file(
        SourceFile, SourceTemp, std::filesystem::copy_options::overwrite_existing, Error);
    if (Error)
    {
        return Failure("Could not copy the OBJ into project Content");
    }
    std::filesystem::remove(ProjectSource, Error);
    Error.clear();
    std::filesystem::rename(SourceTemp, ProjectSource, Error);
    if (Error)
    {
        std::filesystem::remove(SourceTemp);
        return Failure("Could not publish the project-local OBJ source");
    }

    std::filesystem::create_directories(DestinationFile.parent_path(), Error);
    FAssetPath SourceAsset;
    const std::string SourceVirtual = "/Game/" + SourceRelative.generic_string();
    if (Error || !FAssetPath::TryParse(SourceVirtual, SourceAsset))
    {
        return Failure("Could not prepare the imported asset directory");
    }

    EStaticMeshImportError ImportError = EStaticMeshImportError::None;
    if (!ImportObjStaticMeshToFile(ProjectSource, DestinationFile, Options, &ImportError))
    {
        return Failure("OBJ import failed: " + std::string(ToString(ImportError)));
    }
    if (!WriteImportMetadata(DestinationFile, SourceAsset, Options))
    {
        std::filesystem::remove(DestinationFile, Error);
        return Failure("Could not write import metadata");
    }
    return FinishImport(Destination, "Imported " + std::string(Destination.ToString()));
}

FEditorAssetResult FEditorAssetService::ReimportStaticMesh(FAssetPath AssetPath)
{
    FAssetPath SourceAsset;
    FStaticMeshImportOptions Options;
    std::filesystem::path SourceFile;
    std::filesystem::path DestinationFile;
    if (!LoadImportMetadata(AssetPath, SourceAsset, Options)
        || !ResolveContentPath(SourceAsset, SourceFile)
        || !ResolveContentPath(AssetPath, DestinationFile))
    {
        return Failure("Asset has no valid project-local import metadata");
    }
    EStaticMeshImportError ImportError = EStaticMeshImportError::None;
    if (!ImportObjStaticMeshToFile(SourceFile, DestinationFile, Options, &ImportError))
    {
        return Failure("OBJ reimport failed; the previous asset was preserved: "
            + std::string(ToString(ImportError)));
    }
    return FinishImport(AssetPath, "Reimported " + std::string(AssetPath.ToString()));
}

FEditorAssetResult FEditorAssetService::ReimportStaticMeshWithOptions(
    FAssetPath AssetPath,
    const FStaticMeshImportOptions& Options)
{
    FAssetPath SourceAsset;
    FStaticMeshImportOptions PreviousOptions;
    std::filesystem::path SourceFile;
    std::filesystem::path DestinationFile;
    if (!LoadImportMetadata(AssetPath, SourceAsset, PreviousOptions)
        || !ResolveContentPath(SourceAsset, SourceFile)
        || !ResolveContentPath(AssetPath, DestinationFile))
    {
        return Failure("Asset has no valid project-local import metadata");
    }
    EStaticMeshImportError ImportError = EStaticMeshImportError::None;
    if (!ImportObjStaticMeshToFile(SourceFile, DestinationFile, Options, &ImportError))
    {
        return Failure("OBJ reimport failed; the previous asset was preserved: "
            + std::string(ToString(ImportError)));
    }
    if (!WriteImportMetadata(DestinationFile, SourceAsset, Options))
    {
        return Failure("Mesh was rebuilt, but import settings could not be saved");
    }
    return FinishImport(AssetPath, "Reimported " + std::string(AssetPath.ToString()));
}

FEditorAssetResult FEditorAssetService::ImportTexture(
    const std::filesystem::path& SourceFile,
    const FAssetPath& Destination)
{
    if (EngineLoop == nullptr || !Destination.IsValid() || !HasExtension(Destination, ".ptex"))
    {
        return Failure("Texture destination must be a /Game path ending in .ptex");
    }
    FTextureData Texture;
    ETextureImportError ImportError = ETextureImportError::None;
    if (!Pico::ImportTexture(SourceFile, Texture, &ImportError))
    {
        return Failure("Texture import failed: " + std::string(ToString(ImportError)));
    }
    std::filesystem::path DestinationFile;
    if (!ResolveContentPath(Destination, DestinationFile))
    {
        return Failure("Could not resolve the texture destination");
    }
    std::error_code Error;
    if (std::filesystem::exists(DestinationFile, Error))
    {
        return Failure("An asset already exists at " + std::string(Destination.ToString()));
    }
    std::filesystem::path SourceRelative = std::filesystem::path("Source")
        / std::filesystem::path(Destination.GetGameRelativePath());
    SourceRelative.replace_extension(SourceFile.extension());
    std::filesystem::path ProjectSource;
    if (!FPaths::TryGetProjectWritePath(EProjectWriteRoot::Content, SourceRelative, ProjectSource))
    {
        return Failure("Could not create a project-local texture source path");
    }
    std::filesystem::create_directories(ProjectSource.parent_path(), Error);
    const std::filesystem::path SourceTemp = ProjectSource.string() + ".tmp";
    Error.clear();
    std::filesystem::copy_file(
        SourceFile, SourceTemp, std::filesystem::copy_options::overwrite_existing, Error);
    if (Error)
    {
        return Failure("Could not copy the texture into project Content");
    }
    std::filesystem::remove(ProjectSource, Error);
    Error.clear();
    std::filesystem::rename(SourceTemp, ProjectSource, Error);
    if (Error)
    {
        std::filesystem::remove(SourceTemp);
        return Failure("Could not publish the project-local texture source");
    }
    if (!SaveTextureToFile(DestinationFile, Texture))
    {
        return Failure("Could not save the native Texture asset");
    }
    FAssetPath SourceAsset;
    if (!FAssetPath::TryParse("/Game/" + SourceRelative.generic_string(), SourceAsset)
        || !WriteTextureImportMetadata(DestinationFile, SourceAsset))
    {
        std::filesystem::remove(DestinationFile, Error);
        return Failure("Could not save texture import metadata");
    }
    return FinishImport(Destination, "Imported " + std::string(Destination.ToString()));
}

FEditorAssetResult FEditorAssetService::ReimportTexture(FAssetPath AssetPath)
{
    FAssetPath SourceAsset;
    std::filesystem::path SourceFile;
    std::filesystem::path DestinationFile;
    if (!LoadTextureImportMetadata(AssetPath, SourceAsset)
        || !ResolveContentPath(SourceAsset, SourceFile)
        || !ResolveContentPath(AssetPath, DestinationFile))
    {
        return Failure("Texture has no valid project-local import metadata");
    }
    ETextureImportError Error = ETextureImportError::None;
    if (!ImportTextureToFile(SourceFile, DestinationFile, &Error))
    {
        return Failure("Texture reimport failed; the previous asset was preserved: "
            + std::string(ToString(Error)));
    }
    return FinishImport(AssetPath, "Reimported " + std::string(AssetPath.ToString()));
}

FEditorAssetResult FEditorAssetService::CreateMaterial(
    const FAssetPath& Destination,
    const FMaterialData& Material)
{
    std::filesystem::path DestinationFile;
    if (EngineLoop == nullptr || !HasExtension(Destination, ".pmat")
        || !ResolveContentPath(Destination, DestinationFile))
    {
        return Failure("Material destination must be a /Game path ending in .pmat");
    }
    std::error_code Error;
    if (std::filesystem::exists(DestinationFile, Error))
    {
        return Failure("An asset already exists at " + std::string(Destination.ToString()));
    }
    if (!SaveMaterialToFile(DestinationFile, Material))
    {
        return Failure("Could not save the Material asset");
    }
    return FinishImport(Destination, "Created " + std::string(Destination.ToString()));
}

FEditorAssetResult FEditorAssetService::SaveMaterial(
    FAssetPath AssetPath,
    const FMaterialData& Material)
{
    std::filesystem::path FilePath;
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    if (Record == nullptr || Record->Type != EAssetType::Material
        || !ResolveContentPath(AssetPath, FilePath) || !HasExtension(AssetPath, ".pmat"))
    {
        return Failure("Select a valid Material asset");
    }
    if (!SaveMaterialToFile(FilePath, Material))
    {
        return Failure("Could not save Material parameters");
    }
    return FinishImport(AssetPath, "Saved " + std::string(AssetPath.ToString()));
}

FEditorAssetResult FEditorAssetService::RenameAsset(
    FAssetPath AssetPath,
    std::string NewName)
{
    const FAssetRecord* FoundRecord = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    if (FoundRecord == nullptr || FoundRecord->Type == EAssetType::World)
    {
        return Failure("Select a renameable Static Mesh, Texture, or Material asset");
    }
    if (!IsValidAssetName(NewName))
    {
        return Failure("Asset names may only contain letters, numbers, and underscores");
    }

    const FAssetRecord Record = *FoundRecord;
    const std::filesystem::path RelativePath(AssetPath.GetGameRelativePath());
    const std::filesystem::path NewRelativePath =
        RelativePath.parent_path() / (NewName + RelativePath.extension().string());
    FAssetPath NewAssetPath;
    if (!FAssetPath::TryParse(
            "/Game/" + NewRelativePath.generic_string(),
            NewAssetPath))
    {
        return Failure("Could not build the renamed asset path");
    }
    if (NewAssetPath == AssetPath)
    {
        return {true, "Asset name is unchanged", AssetPath};
    }
    if (EngineLoop->GetAssetRegistry().Find(NewAssetPath) != nullptr)
    {
        return Failure("An asset with that name already exists");
    }

    std::filesystem::path OldFile;
    std::filesystem::path NewFile;
    if (!ResolveContentPath(AssetPath, OldFile)
        || !ResolveContentPath(NewAssetPath, NewFile))
    {
        return Failure("Could not resolve the asset inside project Content");
    }

    struct FMaterialUpdate
    {
        FAssetPath AssetPath;
        std::filesystem::path FilePath;
        FMaterialData Before;
        FMaterialData After;
    };
    std::vector<FMaterialUpdate> MaterialUpdates;
    if (Record.Type == EAssetType::Texture)
    {
        for (const FAssetPath& Referencer :
            FAssetDependencyService::FindAssetReferencers(
                AssetPath, EngineLoop->GetAssetRegistry()))
        {
            const FAssetRecord* Candidate =
                EngineLoop->GetAssetRegistry().Find(Referencer);
            if (Candidate == nullptr || Candidate->Type != EAssetType::Material)
            {
                continue;
            }
            FMaterialData Material;
            if (!LoadMaterialFromFile(Candidate->FilePath, Material))
            {
                return Failure(
                    "Could not inspect Material references before renaming the Texture");
            }
            if (Material.BaseColorTexture == AssetPath)
            {
                FMaterialData Updated = Material;
                Updated.BaseColorTexture = NewAssetPath;
                MaterialUpdates.push_back(
                    {Candidate->AssetPath, Candidate->FilePath, Material, Updated});
            }
        }
    }

    const std::filesystem::path OldSidecar = OldFile.string() + ".import";
    const std::filesystem::path NewSidecar = NewFile.string() + ".import";
    std::error_code Error;
    const bool bHasSidecar = std::filesystem::is_regular_file(OldSidecar, Error);
    Error.clear();
    std::filesystem::rename(OldFile, NewFile, Error);
    if (Error)
    {
        return Failure("Could not rename the asset file");
    }
    if (bHasSidecar)
    {
        std::filesystem::rename(OldSidecar, NewSidecar, Error);
        if (Error)
        {
            std::error_code RollbackError;
            std::filesystem::rename(NewFile, OldFile, RollbackError);
            return Failure("Could not rename the asset import metadata");
        }
    }

    std::size_t SavedMaterialCount = 0;
    for (const FMaterialUpdate& Update : MaterialUpdates)
    {
        if (!SaveMaterialToFile(Update.FilePath, Update.After))
        {
            for (std::size_t Index = 0; Index < SavedMaterialCount; ++Index)
            {
                SaveMaterialToFile(
                    MaterialUpdates[Index].FilePath,
                    MaterialUpdates[Index].Before);
            }
            std::error_code RollbackError;
            if (bHasSidecar)
            {
                std::filesystem::rename(NewSidecar, OldSidecar, RollbackError);
            }
            RollbackError.clear();
            std::filesystem::rename(NewFile, OldFile, RollbackError);
            return Failure("Could not update Material references to the renamed Texture");
        }
        ++SavedMaterialCount;
    }

    EngineLoop->GetAssetManager().Invalidate(AssetPath);
    EngineLoop->GetAssetManager().Invalidate(NewAssetPath);
    for (const FMaterialUpdate& Update : MaterialUpdates)
    {
        EngineLoop->GetAssetManager().Invalidate(Update.AssetPath);
    }
    FEditorAssetResult Refresh = RefreshRegistry();
    if (!Refresh.bSucceeded)
    {
        for (const FMaterialUpdate& Update : MaterialUpdates)
        {
            SaveMaterialToFile(Update.FilePath, Update.Before);
        }
        std::error_code RollbackError;
        if (bHasSidecar)
        {
            std::filesystem::rename(NewSidecar, OldSidecar, RollbackError);
        }
        RollbackError.clear();
        std::filesystem::rename(NewFile, OldFile, RollbackError);
        EngineLoop->GetAssetManager().Invalidate(AssetPath);
        EngineLoop->GetAssetManager().Invalidate(NewAssetPath);
        RefreshRegistry();
        return Failure("Asset Registry refresh failed; the rename was rolled back");
    }
    return {
        true,
        "Renamed " + std::string(AssetPath.ToString())
            + " to " + std::string(NewAssetPath.ToString()),
        NewAssetPath};
}

FEditorAssetResult FEditorAssetService::DeleteStaticMesh(
    FAssetPath AssetPath,
    bool bDeleteProjectSource)
{
    return DeleteStaticMeshes({std::move(AssetPath)}, bDeleteProjectSource);
}

FEditorAssetResult FEditorAssetService::DeleteStaticMeshes(
    std::vector<FAssetPath> AssetPaths,
    bool bDeleteProjectSources)
{
    FStagedAssetDeletion Deletion;
    FEditorAssetResult Result = StageDeleteAssets(
        std::move(AssetPaths), bDeleteProjectSources, Deletion);
    return Result.bSucceeded ? CommitStagedDelete(Deletion) : Result;
}

FEditorAssetResult FEditorAssetService::StageDeleteStaticMeshes(
    std::vector<FAssetPath> AssetPaths,
    bool bDeleteProjectSources,
    FStagedAssetDeletion& OutDeletion)
{
    return StageDeleteAssets(
        std::move(AssetPaths), bDeleteProjectSources, OutDeletion);
}

FEditorAssetResult FEditorAssetService::StageDeleteAssets(
    std::vector<FAssetPath> AssetPaths,
    bool bDeleteProjectSources,
    FStagedAssetDeletion& OutDeletion)
{
    if (OutDeletion.bActive)
    {
        return Failure("A staged asset deletion is already active");
    }
    if (EngineLoop == nullptr || AssetPaths.empty())
    {
        return Failure("Select one or more assets");
    }
    std::vector<FAssetPath> CanonicalPaths;
    std::vector<std::filesystem::path> FilesToDelete;
    std::error_code Error;
    for (const FAssetPath& RequestedPath : AssetPaths)
    {
        const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(RequestedPath);
        if (Record == nullptr || Record->Type == EAssetType::World)
        {
            return Failure(
                "Every selected asset must be a registered Static Mesh, Texture, or Material");
        }
        if (std::find(CanonicalPaths.begin(), CanonicalPaths.end(), Record->AssetPath)
            != CanonicalPaths.end())
        {
            continue;
        }
        const FAssetPath CanonicalPath = Record->AssetPath;
        const std::filesystem::path DestinationFile = Record->FilePath;
        CanonicalPaths.push_back(CanonicalPath);
        FilesToDelete.push_back(DestinationFile);
        const std::filesystem::path MetadataFile = DestinationFile.string() + ".import";
        Error.clear();
        if (std::filesystem::is_regular_file(MetadataFile, Error))
        {
            FilesToDelete.push_back(MetadataFile);
        }
        if (bDeleteProjectSources)
        {
            FAssetPath SourceAsset;
            std::filesystem::path SourceFile;
            bool bHasSource = false;
            if (Record->Type == EAssetType::StaticMesh)
            {
                FStaticMeshImportOptions Options;
                bHasSource = LoadImportMetadata(CanonicalPath, SourceAsset, Options);
            }
            else if (Record->Type == EAssetType::Texture)
            {
                bHasSource = LoadTextureImportMetadata(CanonicalPath, SourceAsset);
            }
            if (bHasSource
                && ResolveContentPath(SourceAsset, SourceFile)
                && std::filesystem::is_regular_file(SourceFile, Error)
                && std::find(FilesToDelete.begin(), FilesToDelete.end(), SourceFile)
                    == FilesToDelete.end())
            {
                FilesToDelete.push_back(SourceFile);
            }
        }
    }

    std::vector<FStagedAssetDeletionFile> RenamedFiles;
    for (const std::filesystem::path& File : FilesToDelete)
    {
        std::filesystem::path Temporary = File.string() + ".delete_tmp";
        std::filesystem::remove(Temporary, Error);
        Error.clear();
        std::filesystem::rename(File, Temporary, Error);
        if (Error)
        {
            for (auto It = RenamedFiles.rbegin(); It != RenamedFiles.rend(); ++It)
            {
                std::error_code RestoreError;
                std::filesystem::rename(It->Temporary, It->Original, RestoreError);
            }
            return Failure("Could not stage all asset files for deletion");
        }
        RenamedFiles.push_back({File, Temporary});
    }

    FAssetScanReport Report;
    if (!EngineLoop->GetAssetRegistry().ScanProjectContent(&Report))
    {
        for (auto It = RenamedFiles.rbegin(); It != RenamedFiles.rend(); ++It)
        {
            std::error_code RestoreError;
            std::filesystem::rename(It->Temporary, It->Original, RestoreError);
        }
        EngineLoop->GetAssetRegistry().ScanProjectContent();
        return Failure("Could not refresh Registry; asset deletion was rolled back");
    }
    OutDeletion.AssetPaths = std::move(CanonicalPaths);
    OutDeletion.Files = std::move(RenamedFiles);
    OutDeletion.bDeleteProjectSources = bDeleteProjectSources;
    OutDeletion.bActive = true;
    return {
        true,
        "Staged " + std::to_string(OutDeletion.AssetPaths.size())
            + " asset(s) for deletion",
        {}};
}

FEditorAssetResult FEditorAssetService::ClearStagedAssetReferences(
    FStagedAssetDeletion& Deletion)
{
    if (EngineLoop == nullptr || !Deletion.bActive)
    {
        return Failure("No staged asset deletion to clean up");
    }

    std::size_t ClearedCount = 0;
    for (const FAssetRecord& Record : EngineLoop->GetAssetRegistry().GetAssets())
    {
        if (Record.Type != EAssetType::Material)
        {
            continue;
        }
        FMaterialData Material;
        if (!LoadMaterialFromFile(Record.FilePath, Material)
            || std::find(
                Deletion.AssetPaths.begin(),
                Deletion.AssetPaths.end(),
                Material.BaseColorTexture) == Deletion.AssetPaths.end())
        {
            continue;
        }

        Deletion.MaterialReferenceEdits.push_back(
            {Record.AssetPath, Record.FilePath, Material});
        Material.BaseColorTexture = {};
        if (!SaveMaterialToFile(Record.FilePath, Material))
        {
            for (auto It = Deletion.MaterialReferenceEdits.rbegin();
                It != Deletion.MaterialReferenceEdits.rend(); ++It)
            {
                SaveMaterialToFile(It->FilePath, It->Before);
                EngineLoop->GetAssetManager().Invalidate(It->AssetPath);
            }
            Deletion.MaterialReferenceEdits.clear();
            return Failure("Could not clear every Material asset reference");
        }
        EngineLoop->GetAssetManager().Invalidate(Record.AssetPath);
        ++ClearedCount;
    }
    return {
        true,
        "Cleared " + std::to_string(ClearedCount) + " asset reference(s)",
        {}};
}

FEditorAssetResult FEditorAssetService::CommitStagedDelete(
    FStagedAssetDeletion& Deletion)
{
    if (EngineLoop == nullptr || !Deletion.bActive)
    {
        return Failure("No staged asset deletion to commit");
    }
    for (const FAssetPath& AssetPath : Deletion.AssetPaths)
    {
        EngineLoop->GetAssetManager().Invalidate(AssetPath);
    }
    std::error_code Error;
    bool bCleanupSucceeded = true;
    for (const FStagedAssetDeletionFile& File : Deletion.Files)
    {
        Error.clear();
        if (!std::filesystem::remove(File.Temporary, Error) || Error)
        {
            bCleanupSucceeded = false;
        }
    }
    const std::size_t AssetCount = Deletion.AssetPaths.size();
    const bool bDeletedSources = Deletion.bDeleteProjectSources;
    Deletion = {};
    return {
        true,
        "Deleted " + std::to_string(AssetCount) + " asset(s)"
            + (bDeletedSources ? " and project source(s)" : "")
            + (bCleanupSucceeded ? "" : "; some temporary files need cleanup"),
        {}};
}

FEditorAssetResult FEditorAssetService::RollbackStagedDelete(
    FStagedAssetDeletion& Deletion)
{
    if (EngineLoop == nullptr || !Deletion.bActive)
    {
        return Failure("No staged asset deletion to roll back");
    }
    bool bRestored = true;
    for (auto It = Deletion.MaterialReferenceEdits.rbegin();
        It != Deletion.MaterialReferenceEdits.rend(); ++It)
    {
        const bool bSaved = SaveMaterialToFile(It->FilePath, It->Before);
        EngineLoop->GetAssetManager().Invalidate(It->AssetPath);
        bRestored = bRestored && bSaved;
    }
    for (auto It = Deletion.Files.rbegin(); It != Deletion.Files.rend(); ++It)
    {
        std::error_code Error;
        std::filesystem::rename(It->Temporary, It->Original, Error);
        bRestored = bRestored && !Error;
    }
    FAssetScanReport Report;
    const bool bRegistryRestored =
        EngineLoop->GetAssetRegistry().ScanProjectContent(&Report);
    Deletion = {};
    return bRestored && bRegistryRestored
        ? FEditorAssetResult {true, "Asset deletion was rolled back", {}}
        : Failure("Asset deletion rollback was incomplete");
}

bool FEditorAssetService::CanReimportStaticMesh(const FAssetPath& AssetPath) const
{
    std::filesystem::path Destination;
    std::error_code Error;
    return HasExtension(AssetPath, ".pmesh")
        && ResolveContentPath(AssetPath, Destination)
        && std::filesystem::is_regular_file(Destination.string() + ".import", Error);
}

bool FEditorAssetService::CanReimportTexture(const FAssetPath& AssetPath) const
{
    FAssetPath Source;
    std::filesystem::path SourceFile;
    std::error_code Error;
    return HasExtension(AssetPath, ".ptex")
        && LoadTextureImportMetadata(AssetPath, Source)
        && ResolveContentPath(Source, SourceFile)
        && std::filesystem::is_regular_file(SourceFile, Error);
}

bool FEditorAssetService::GetStaticMeshImportSettings(
    const FAssetPath& AssetPath,
    std::filesystem::path& OutSourceFile,
    FStaticMeshImportOptions& OutOptions) const
{
    FAssetPath SourceAsset;
    return LoadImportMetadata(AssetPath, SourceAsset, OutOptions)
        && ResolveContentPath(SourceAsset, OutSourceFile);
}

bool FEditorAssetService::ResolveContentPath(
    const FAssetPath& AssetPath,
    std::filesystem::path& OutPath) const
{
    return AssetPath.IsValid() && FPaths::TryGetProjectWritePath(
        EProjectWriteRoot::Content,
        std::filesystem::path(AssetPath.GetGameRelativePath()),
        OutPath);
}

bool FEditorAssetService::LoadImportMetadata(
    const FAssetPath& AssetPath,
    FAssetPath& OutSource,
    FStaticMeshImportOptions& OutOptions) const
{
    std::filesystem::path Destination;
    if (!ResolveContentPath(AssetPath, Destination))
    {
        return false;
    }
    FConfigFile Config;
    if (!Config.Load(std::filesystem::path(Destination.string() + ".import")))
    {
        return false;
    }
    const std::string Source = Config.GetString("PicoStaticMeshImport", "Source", "");
    if (!FAssetPath::TryParse(Source, OutSource))
    {
        return false;
    }
    OutOptions.UniformScale = static_cast<float>(
        Config.GetDouble("PicoStaticMeshImport", "UniformScale", 1.0));
    OutOptions.bConvertYUpToZUp = Config.GetBool(
        "PicoStaticMeshImport", "ConvertYUpToZUp", true);
    OutOptions.bFlipTexCoordV = Config.GetBool(
        "PicoStaticMeshImport", "FlipTexCoordV", true);
    return true;
}

bool FEditorAssetService::WriteImportMetadata(
    const std::filesystem::path& DestinationFile,
    const FAssetPath& SourceAsset,
    const FStaticMeshImportOptions& Options) const
{
    const std::filesystem::path MetadataFile = DestinationFile.string() + ".import";
    const std::filesystem::path TempFile = MetadataFile.string() + ".tmp";
    std::ofstream Output(TempFile, std::ios::trunc);
    if (!Output)
    {
        return false;
    }
    Output << "[PicoStaticMeshImport]\n"
        << "Version=1\n"
        << "Source=" << SourceAsset.ToString() << '\n'
        << "UniformScale=" << Options.UniformScale << '\n'
        << "ConvertYUpToZUp=" << (Options.bConvertYUpToZUp ? "true" : "false") << '\n'
        << "FlipTexCoordV=" << (Options.bFlipTexCoordV ? "true" : "false") << '\n';
    Output.close();
    if (!Output)
    {
        std::filesystem::remove(TempFile);
        return false;
    }
    std::error_code Error;
    std::filesystem::remove(MetadataFile, Error);
    Error.clear();
    std::filesystem::rename(TempFile, MetadataFile, Error);
    if (Error)
    {
        std::filesystem::remove(TempFile);
    }
    return !Error;
}

bool FEditorAssetService::LoadTextureImportMetadata(
    const FAssetPath& AssetPath,
    FAssetPath& OutSource) const
{
    std::filesystem::path Destination;
    if (!ResolveContentPath(AssetPath, Destination)) return false;
    FConfigFile Config;
    if (!Config.Load(std::filesystem::path(Destination.string() + ".import"))) return false;
    return FAssetPath::TryParse(
        Config.GetString("PicoTextureImport", "Source", ""), OutSource);
}

bool FEditorAssetService::WriteTextureImportMetadata(
    const std::filesystem::path& DestinationFile,
    const FAssetPath& SourceAsset) const
{
    const std::filesystem::path MetadataFile = DestinationFile.string() + ".import";
    const std::filesystem::path TempFile = MetadataFile.string() + ".tmp";
    std::ofstream Output(TempFile, std::ios::trunc);
    if (!Output) return false;
    Output << "[PicoTextureImport]\nVersion=1\nSource="
        << SourceAsset.ToString() << '\n';
    Output.close();
    if (!Output)
    {
        std::filesystem::remove(TempFile);
        return false;
    }
    std::error_code Error;
    std::filesystem::remove(MetadataFile, Error);
    Error.clear();
    std::filesystem::rename(TempFile, MetadataFile, Error);
    if (Error) std::filesystem::remove(TempFile);
    return !Error;
}

FEditorAssetResult FEditorAssetService::FinishImport(
    FAssetPath AssetPath,
    std::string Message)
{
    EngineLoop->GetAssetManager().Invalidate(AssetPath);
    FEditorAssetResult Refresh = RefreshRegistry();
    if (!Refresh.bSucceeded)
    {
        return Failure(std::move(Refresh.Message));
    }
    return {true, std::move(Message), AssetPath};
}
}
