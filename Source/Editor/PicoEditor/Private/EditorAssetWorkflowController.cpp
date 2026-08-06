#include "EditorAssetWorkflowController.h"
#include "Pico/Editor/AssetDependencyService.h"

#include "NativeFileDialog.h"

#include "Pico/Editor/EditorAssetService.h"
#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace Pico
{
namespace
{
std::string MakeAssetName(const std::filesystem::path& SourceFile)
{
    std::string Name = SourceFile.stem().string();
    for (char& Character : Name)
    {
        const unsigned char Value = static_cast<unsigned char>(Character);
        if (!std::isalnum(Value) && Character != '_')
        {
            Character = '_';
        }
    }
    return Name.empty() ? "StaticMesh" : Name;
}
}

FEditorAssetWorkflowController::FEditorAssetWorkflowController(
    FEngineLoop* InEngineLoop,
    FEditorAssetService* InAssetService,
    FEditorCommandService* InCommandService,
    FEditorAssetSelection* InAssetSelection,
    FStatus InSetStatus,
    FInvalidateStaticMesh InInvalidateStaticMesh)
    : EngineLoop(InEngineLoop)
    , AssetService(InAssetService)
    , CommandService(InCommandService)
    , AssetSelection(InAssetSelection)
    , SetStatus(std::move(InSetStatus))
    , InvalidateStaticMesh(std::move(InInvalidateStaticMesh))
{
}

void FEditorAssetWorkflowController::Draw()
{
    if (const auto Request = ImportDialog.Draw())
    {
        ConfirmImport(*Request);
    }
    if (const auto Request = DeleteDialog.Draw())
    {
        ConfirmDelete(*Request);
    }
    if (const auto Request = MaterialDialog.Draw())
    {
        ConfirmMaterial(*Request);
    }
    DrawRenameDialog();
}

void FEditorAssetWorkflowController::OpenTextureImport()
{
    if (AssetService == nullptr) return;
    const auto SourceFile = OpenTextureFileDialog();
    if (!SourceFile.has_value()) return;
    const FAssetPath Destination = FindUniqueAssetPath(
        "/Game/Textures", MakeAssetName(*SourceFile), ".ptex");
    Report(Destination.IsValid()
        ? AssetService->ImportTexture(*SourceFile, Destination)
        : FEditorAssetResult {false, "Could not choose a unique Texture name", {}});
    if (Destination.IsValid() && AssetSelection != nullptr
        && EngineLoop->GetAssetRegistry().Find(Destination) != nullptr)
    {
        AssetSelection->Select(Destination);
    }
}

void FEditorAssetWorkflowController::OpenCreateMaterial()
{
    const FAssetPath Destination = FindUniqueAssetPath(
        "/Game/Materials", "Material", ".pmat");
    if (!Destination.IsValid())
    {
        SetStatus("Could not choose a unique Material name", true);
        return;
    }
    MaterialDialog.OpenCreate(
        Destination, GetSelectedTexture(), EngineLoop->GetAssetRegistry());
}

void FEditorAssetWorkflowController::OpenEditMaterial(const FAssetPath& AssetPath)
{
    const std::shared_ptr<const FMaterialData> Material = EngineLoop != nullptr
        ? EngineLoop->GetAssetManager().LoadMaterial(
            AssetPath, EngineLoop->GetAssetRegistry()) : nullptr;
    if (Material == nullptr)
    {
        SetStatus("Could not load the selected Material", true);
        return;
    }
    MaterialDialog.OpenEdit(
        AssetPath, *Material, GetSelectedTexture(), EngineLoop->GetAssetRegistry());
}

void FEditorAssetWorkflowController::OpenImport()
{
    if (AssetService == nullptr)
    {
        return;
    }
    const auto SourceFile = OpenObjFileDialog();
    if (!SourceFile.has_value())
    {
        return;
    }
    FStaticMeshSourceAnalysis Analysis;
    FEditorAssetResult Result = AssetService->AnalyzeStaticMeshSource(*SourceFile, Analysis);
    if (!Result.bSucceeded)
    {
        Report(std::move(Result));
        return;
    }
    ImportDialog.Open(*SourceFile, {}, Analysis, {}, true);
}

void FEditorAssetWorkflowController::OpenReimportOptions(const FAssetPath& AssetPath)
{
    if (AssetService == nullptr)
    {
        return;
    }
    FStaticMeshImportOptions Options;
    std::filesystem::path SourceFile;
    if (!AssetService->GetStaticMeshImportSettings(AssetPath, SourceFile, Options))
    {
        SetStatus("Asset has no valid import settings", true);
        return;
    }
    FStaticMeshSourceAnalysis Analysis;
    FEditorAssetResult Result = AssetService->AnalyzeStaticMeshSource(SourceFile, Analysis);
    if (!Result.bSucceeded)
    {
        Report(std::move(Result));
        return;
    }
    ImportDialog.Open(std::move(SourceFile), AssetPath, Analysis, Options, false);
}

void FEditorAssetWorkflowController::OpenDelete(
    const std::vector<FAssetPath>& AssetPaths)
{
    if (EngineLoop == nullptr)
    {
        return;
    }
    std::vector<FAssetPath> CanonicalPaths;
    for (const FAssetPath& AssetPath : AssetPaths)
    {
        const FAssetRecord* Record = EngineLoop->GetAssetRegistry().Find(AssetPath);
        if (Record != nullptr
            && Record->Type != EAssetType::World
            && std::find(CanonicalPaths.begin(), CanonicalPaths.end(), Record->AssetPath)
                == CanonicalPaths.end())
        {
            CanonicalPaths.push_back(Record->AssetPath);
        }
    }
    if (CanonicalPaths.empty())
    {
        SetStatus("Select one or more Static Mesh, Texture, or Material assets to delete", true);
        return;
    }
    DeleteDialog.Open(CanonicalPaths, FindAssetReferences(CanonicalPaths));
}

void FEditorAssetWorkflowController::OpenRename(const FAssetPath& AssetPath)
{
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    if (Record == nullptr || Record->Type == EAssetType::World)
    {
        SetStatus("Select one Static Mesh, Texture, or Material asset to rename", true);
        return;
    }
    RenameAssetPath = Record->AssetPath;
    RenameBuffer.fill('\0');
    const std::string Name =
        std::filesystem::path(Record->AssetPath.GetGameRelativePath()).stem().string();
    const std::size_t CopyLength = std::min(Name.size(), RenameBuffer.size() - 1);
    std::copy_n(Name.data(), CopyLength, RenameBuffer.data());
    bOpenRenamePopup = true;
}

void FEditorAssetWorkflowController::DrawRenameDialog()
{
    if (bOpenRenamePopup)
    {
        ImGui::OpenPopup("Rename Asset");
        bOpenRenamePopup = false;
    }
    if (!ImGui::BeginPopupModal(
            "Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    ImGui::Text("Rename %s", RenameAssetPath.ToString().data());
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool bSubmitted = ImGui::InputText(
        "##AssetName",
        RenameBuffer.data(),
        RenameBuffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (bSubmitted || ImGui::Button("Rename"))
    {
        ConfirmRename();
        if (!RenameAssetPath.IsValid())
        {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        RenameAssetPath = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FEditorAssetWorkflowController::ConfirmRename()
{
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(RenameAssetPath) : nullptr;
    if (Record == nullptr || AssetService == nullptr || CommandService == nullptr)
    {
        SetStatus("Selected asset is no longer available", true);
        return;
    }
    const FAssetPath OldPath = Record->AssetPath;
    const EAssetType AssetType = Record->Type;
    FEditorAssetResult Result = AssetService->RenameAsset(
        OldPath, std::string(RenameBuffer.data()));
    if (!Result.bSucceeded)
    {
        Report(std::move(Result));
        return;
    }
    const FEditorCommandResult ReferenceResult =
        CommandService->ReplaceAssetReferences(
            OldPath, Result.AssetPath, AssetType);
    if (!ReferenceResult.bSucceeded)
    {
        SetStatus(ReferenceResult.Message, true);
        return;
    }
    if (AssetSelection != nullptr)
    {
        AssetSelection->Select(Result.AssetPath);
    }
    RenameAssetPath = {};
    Report(std::move(Result));
}

void FEditorAssetWorkflowController::RefreshRegistry()
{
    if (AssetService == nullptr)
    {
        return;
    }
    FEditorAssetResult Result = AssetService->RefreshRegistry();
    if (Result.bSucceeded && AssetSelection != nullptr && EngineLoop != nullptr)
    {
        AssetSelection->Validate(EngineLoop->GetAssetRegistry());
    }
    Report(std::move(Result));
}

void FEditorAssetWorkflowController::Reimport(const FAssetPath& AssetPath)
{
    if (AssetService == nullptr)
    {
        return;
    }
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    FEditorAssetResult Result = Record != nullptr && Record->Type == EAssetType::Texture
        ? AssetService->ReimportTexture(AssetPath)
        : AssetService->ReimportStaticMesh(AssetPath);
    if (Result.bSucceeded && AssetSelection != nullptr)
    {
        AssetSelection->Select(Result.AssetPath);
    }
    Report(std::move(Result));
}

bool FEditorAssetWorkflowController::CanReimport(const FAssetPath& AssetPath) const
{
    const FAssetRecord* Record = EngineLoop != nullptr
        ? EngineLoop->GetAssetRegistry().Find(AssetPath) : nullptr;
    if (Record == nullptr) return false;
    if (Record->Type == EAssetType::StaticMesh)
    {
        return AssetService->CanReimportStaticMesh(AssetPath);
    }
    if (Record->Type == EAssetType::Texture)
    {
        return AssetService->CanReimportTexture(AssetPath);
    }
    return false;
}

void FEditorAssetWorkflowController::ConfirmImport(
    const FStaticMeshImportRequest& Request)
{
    FEditorAssetResult Result;
    if (Request.ReimportAsset.IsValid())
    {
        Result = AssetService->ReimportStaticMeshWithOptions(
            Request.ReimportAsset, Request.Options);
    }
    else
    {
        const std::string BaseName = MakeAssetName(Request.SourceFile);
        FAssetPath Destination;
        for (unsigned int Suffix = 1; Suffix < 10000; ++Suffix)
        {
            const std::string Name = Suffix == 1
                ? BaseName : BaseName + "_" + std::to_string(Suffix);
            FAssetPath Candidate;
            if (FAssetPath::TryParse("/Game/Meshes/" + Name + ".pmesh", Candidate)
                && EngineLoop->GetAssetRegistry().Find(Candidate) == nullptr)
            {
                Destination = Candidate;
                break;
            }
        }
        Result = Destination.IsValid()
            ? AssetService->ImportStaticMesh(Request.SourceFile, Destination, Request.Options)
            : FEditorAssetResult {
                false, "Could not choose a unique imported asset name", {}};
    }
    if (Result.bSucceeded && AssetSelection != nullptr)
    {
        AssetSelection->Select(Result.AssetPath);
    }
    Report(std::move(Result));
}

void FEditorAssetWorkflowController::ConfirmDelete(
    const FDeleteAssetsRequest& Request)
{
    FStagedAssetDeletion Deletion;
    FEditorAssetResult StageResult = AssetService->StageDeleteAssets(
        Request.AssetPaths, Request.bDeleteProjectSources, Deletion);
    if (!StageResult.bSucceeded)
    {
        Report(std::move(StageResult));
        return;
    }

    if (Request.bClearSceneReferences)
    {
        FEditorAssetResult AssetClearResult =
            AssetService->ClearStagedAssetReferences(Deletion);
        if (!AssetClearResult.bSucceeded)
        {
            FEditorAssetResult RollbackResult = AssetService->RollbackStagedDelete(Deletion);
            SetStatus(
                AssetClearResult.Message + (RollbackResult.bSucceeded
                    ? "; asset files were restored"
                    : "; " + RollbackResult.Message),
                true);
            return;
        }
        FEditorCommandResult ClearResult =
            CommandService->ClearAssetReferences(Deletion.AssetPaths);
        if (!ClearResult.bSucceeded)
        {
            FEditorAssetResult RollbackResult = AssetService->RollbackStagedDelete(Deletion);
            SetStatus(
                ClearResult.Message + (RollbackResult.bSucceeded
                    ? "; asset files were restored"
                    : "; " + RollbackResult.Message),
                true);
            return;
        }
    }

    const std::vector<FAssetPath> DeletedPaths = Deletion.AssetPaths;
    FEditorAssetResult Result = AssetService->CommitStagedDelete(Deletion);
    if (Result.bSucceeded)
    {
        for (const FAssetPath& AssetPath : DeletedPaths)
        {
            InvalidateStaticMesh(AssetPath);
        }
        if (AssetSelection != nullptr)
        {
            AssetSelection->Clear();
        }
    }
    Report(std::move(Result));
}

void FEditorAssetWorkflowController::ConfirmMaterial(
    const FMaterialSaveRequest& Request)
{
    FEditorAssetResult Result = Request.bCreate
        ? AssetService->CreateMaterial(Request.AssetPath, Request.Material)
        : AssetService->SaveMaterial(Request.AssetPath, Request.Material);
    if (Result.bSucceeded && AssetSelection != nullptr)
    {
        AssetSelection->Select(Result.AssetPath);
    }
    Report(std::move(Result));
}

FAssetPath FEditorAssetWorkflowController::FindUniqueAssetPath(
    std::string_view Folder,
    std::string_view BaseName,
    std::string_view Extension) const
{
    if (EngineLoop == nullptr) return {};
    for (unsigned int Suffix = 1; Suffix < 10000; ++Suffix)
    {
        const std::string Name = Suffix == 1
            ? std::string(BaseName)
            : std::string(BaseName) + "_" + std::to_string(Suffix);
        FAssetPath Candidate;
        if (FAssetPath::TryParse(
                std::string(Folder) + "/" + Name + std::string(Extension), Candidate)
            && EngineLoop->GetAssetRegistry().Find(Candidate) == nullptr)
        {
            return Candidate;
        }
    }
    return {};
}

FAssetPath FEditorAssetWorkflowController::GetSelectedTexture() const
{
    const FAssetRecord* Record = EngineLoop != nullptr && AssetSelection != nullptr
        ? AssetSelection->Resolve(EngineLoop->GetAssetRegistry()) : nullptr;
    return Record != nullptr && Record->Type == EAssetType::Texture
        ? Record->AssetPath : FAssetPath {};
}

std::vector<std::string> FEditorAssetWorkflowController::FindAssetReferences(
    const std::vector<FAssetPath>& AssetPaths) const
{
    std::vector<std::string> References;
    PWorld* World = EngineLoop != nullptr ? EngineLoop->GetWorld() : nullptr;
    if (World == nullptr)
    {
        return References;
    }
    for (const FObjectAssetReference& Reference :
        FAssetDependencyService::GatherWorldReferences(World))
    {
        if (std::find(AssetPaths.begin(), AssetPaths.end(), Reference.AssetPath)
            != AssetPaths.end())
        {
            References.push_back(Reference.ObjectPath);
        }
    }
    const FAssetRegistry& Registry = EngineLoop->GetAssetRegistry();
    for (const FAssetPath& AssetPath : AssetPaths)
    {
        for (const FAssetPath& Referencer :
            FAssetDependencyService::FindAssetReferencers(AssetPath, Registry))
        {
            if (std::find(AssetPaths.begin(), AssetPaths.end(), Referencer)
                == AssetPaths.end())
            {
                References.push_back(
                    std::string(Referencer.ToString()) + " -> "
                    + std::string(AssetPath.ToString()));
            }
        }
    }
    std::sort(References.begin(), References.end());
    References.erase(std::unique(References.begin(), References.end()), References.end());
    return References;
}

void FEditorAssetWorkflowController::Report(FEditorAssetResult Result)
{
    SetStatus(std::move(Result.Message), !Result.bSucceeded);
}
}
