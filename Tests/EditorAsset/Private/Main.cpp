#include "TestRunner.h"

#include "Pico/Core/Paths.h"
#include "Pico/Editor/EditorAssetSelection.h"
#include "Pico/Editor/EditorAssetService.h"
#include "Pico/Editor/AssetDependencyService.h"
#include "Pico/Editor/EditorCommandService.h"
#include "Pico/Editor/EditorPropertyService.h"
#include "Pico/Editor/EditorSceneClipboard.h"
#include "Pico/Editor/EditorSelection.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectRegistry.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
void WriteText(const std::filesystem::path& Path, std::string_view Text)
{
    std::filesystem::create_directories(Path.parent_path());
    std::ofstream File(Path, std::ios::trunc);
    File << Text;
}

std::vector<char> ReadBytes(const std::filesystem::path& Path)
{
    std::ifstream File(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
}

std::string MakeTriangleObj(float Height)
{
    return "o Triangle\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 " + std::to_string(Height) + " 0\n"
        "f 1 2 3\n";
}

void TestEditorAssetWorkflow(FTestRunner& Runner)
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("PicoEditorAssetTests_" + std::to_string(Suffix));
    const std::filesystem::path ProjectFile = Root / "EditorAssetTest.pico";
    const std::filesystem::path ExternalObj = Root / "External" / "Triangle.obj";
    const std::filesystem::path ExternalTexture = Root / "External" / "Colors.ppm";
    WriteText(ProjectFile,
        "[Project]\nName=EditorAssetTest\nFileVersion=1\nEngineVersion=0.1.0\n");
    WriteText(ExternalObj, MakeTriangleObj(1.0f));
    std::string Ppm = "P6\n2 1\n255\n";
    constexpr unsigned char TexturePixels[] = {255, 0, 0, 0, 255, 0};
    for (const unsigned char Value : TexturePixels)
    {
        Ppm.push_back(static_cast<char>(Value));
    }
    WriteText(ExternalTexture, Ppm);

    char Program[] = "PicoEditorAssetTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = {Program, MaxFPS};
    Pico::FEngineLoop EngineLoop;
    Runner.Expect(
        EngineLoop.PreInit(2, Arguments, ProjectFile) == 0 && EngineLoop.Init() == 0,
        "Editor asset test initializes a project EngineLoop");

    Pico::FEditorAssetService Service(&EngineLoop);
    Pico::FStaticMeshSourceAnalysis Analysis;
    const Pico::FEditorAssetResult Analyze =
        Service.AnalyzeStaticMeshSource(ExternalObj, Analysis);
    Runner.Expect(
        Analyze.bSucceeded
            && Analysis.VertexCount == 3
            && Analysis.TriangleCount == 1
            && Analysis.Bounds.Max.Y == 1.0f,
        "Editor asset service analyzes source geometry before import");
    Pico::FAssetPath AssetPath;
    Pico::FAssetPath::TryParse("/Game/Meshes/Triangle.pmesh", AssetPath);
    const Pico::FEditorAssetResult Import = Service.ImportStaticMesh(ExternalObj, AssetPath);
    const std::filesystem::path ImportedFile = Root / "Content" / "Meshes" / "Triangle.pmesh";
    const std::filesystem::path SourceFile = Root / "Content" / "Source" / "Meshes" / "Triangle.obj";
    const std::filesystem::path MetadataFile = ImportedFile.string() + ".import";
    Runner.Expect(
        Import.bSucceeded
            && std::filesystem::is_regular_file(ImportedFile)
            && std::filesystem::is_regular_file(SourceFile)
            && std::filesystem::is_regular_file(MetadataFile),
        "OBJ import creates native data, project-local source and sidecar metadata");
    Runner.Expect(
        EngineLoop.GetAssetRegistry().Find(AssetPath) != nullptr,
        "Successful import refreshes the Asset Registry");

    Pico::FEditorAssetSelection Selection;
    Selection.Select(AssetPath);
    Runner.Expect(
        Selection.Validate(EngineLoop.GetAssetRegistry())
            && Selection.Resolve(EngineLoop.GetAssetRegistry()) != nullptr,
        "Asset selection resolves from its stable virtual path");

    const auto FirstMesh = EngineLoop.GetAssetManager().LoadStaticMesh(
        AssetPath, EngineLoop.GetAssetRegistry());
    WriteText(SourceFile, MakeTriangleObj(3.0f));
    const Pico::FAssetRecord* RegistryRecord =
        EngineLoop.GetAssetRegistry().Find(AssetPath);
    const Pico::FEditorAssetResult Reimport = RegistryRecord != nullptr
        ? Service.ReimportStaticMesh(RegistryRecord->AssetPath)
        : Pico::FEditorAssetResult {};
    const auto ReimportedMesh = EngineLoop.GetAssetManager().LoadStaticMesh(
        AssetPath, EngineLoop.GetAssetRegistry());
    Runner.Expect(
        Reimport.bSucceeded
            && FirstMesh != nullptr
            && ReimportedMesh != nullptr
            && ReimportedMesh != FirstMesh
            && ReimportedMesh->Bounds.Max.Z == 3.0f,
        "Reimport uses metadata and invalidates cached CPU mesh data");

    Pico::PActor* Actor = EngineLoop.GetWorld()->SpawnActor<Pico::PActor>("MeshActor");
    Pico::PStaticMeshComponent* Component = Actor != nullptr
        ? Actor->CreateComponent<Pico::PStaticMeshComponent>("Mesh") : nullptr;
    const bool bSceneReady = Actor != nullptr && Component != nullptr
        && Actor->SetRootComponent(Component);
    Pico::FEditorSelection ObjectSelection;
    ObjectSelection.Set(Component);
    Pico::FEditorTransactionManager Transactions;
    Pico::FEditorSceneClipboard Clipboard;
    Pico::FEditorCommandService Commands(
        &EngineLoop, &ObjectSelection, &Transactions, &Clipboard);
    Pico::FEditorPropertyService PropertyService(
        &EngineLoop,
        &ObjectSelection,
        &Transactions,
        [&EngineLoop, &ObjectSelection](
            const Pico::FEditorWorldSnapshot& Snapshot,
            Pico::EWorldSerializationError* Error)
        {
            if (!EngineLoop.ReplaceWorld(Snapshot.WorldData, Error))
            {
                return false;
            }
            ObjectSelection.Restore(
                EngineLoop.GetWorld(),
                Snapshot.SelectedObjectPaths,
                Snapshot.PrimaryObjectPath);
            return true;
        });
    Runner.Expect(
        bSceneReady
            && Commands.AssignStaticMeshAsset(AssetPath).bSucceeded
            && Component->GetStaticMeshAsset() == AssetPath,
        "Static Mesh assignment is routed through the editor command service");
    Runner.Expect(
        Commands.Undo().bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetStaticMeshAsset().IsEmpty(),
        "Static Mesh assignment participates in scene Undo");
    Runner.Expect(
        Commands.Redo().bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetStaticMeshAsset() == AssetPath,
        "Static Mesh assignment participates in scene Redo");

    Pico::FAssetPath TexturePath;
    Pico::FAssetPath MaterialPath;
    Pico::FAssetPath::TryParse("/Game/Textures/Colors.ptex", TexturePath);
    Pico::FAssetPath::TryParse("/Game/Materials/TestMaterial.pmat", MaterialPath);
    const Pico::FEditorAssetResult TextureImport =
        Service.ImportTexture(ExternalTexture, TexturePath);
    Pico::FMaterialData Material;
    Material.BaseColor = Pico::FVector3(0.8f, 0.25f, 0.1f);
    Material.Metallic = 0.7f;
    Material.Roughness = 0.2f;
    Material.BaseColorTexture = TexturePath;
    const Pico::FEditorAssetResult MaterialCreate =
        Service.CreateMaterial(MaterialPath, Material);
    Runner.Expect(
        TextureImport.bSucceeded
            && MaterialCreate.bSucceeded
            && EngineLoop.GetAssetManager().LoadTexture(
                TexturePath, EngineLoop.GetAssetRegistry()) != nullptr
            && EngineLoop.GetAssetManager().LoadMaterial(
                MaterialPath, EngineLoop.GetAssetRegistry()) != nullptr,
        "Editor asset service imports Texture and creates Material assets");
    Runner.Expect(
        PropertyService.SetProperty(
            ObjectSelection.GetHandle(),
            Pico::FName("MaterialAsset"),
            MaterialPath).bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetMaterialAsset() == MaterialPath,
        "Material assignment is routed through the generic property service");
    const std::vector<Pico::FAssetPath> MaterialDependencies =
        Pico::FAssetDependencyService::GetAssetDependencies(
            MaterialPath, EngineLoop.GetAssetRegistry());
    const std::vector<Pico::FObjectAssetReference> WorldReferences =
        Pico::FAssetDependencyService::GatherWorldReferences(EngineLoop.GetWorld());
    Runner.Expect(
        std::find(
            MaterialDependencies.begin(),
            MaterialDependencies.end(),
            TexturePath) != MaterialDependencies.end()
            && std::any_of(
                WorldReferences.begin(),
                WorldReferences.end(),
                [&MaterialPath](const Pico::FObjectAssetReference& Reference)
                {
                    return Reference.AssetPath == MaterialPath
                        && Reference.PropertyName == Pico::FName("MaterialAsset");
                }),
        "Dependency service discovers asset-to-asset and world-to-asset references");
    Runner.Expect(
        Commands.Undo().bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetMaterialAsset().IsEmpty(),
        "Material assignment participates in scene Undo");
    Runner.Expect(
        Commands.Redo().bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetMaterialAsset() == MaterialPath,
        "Material assignment participates in scene Redo");
    Material.Roughness = 0.85f;
    const Pico::FEditorAssetResult MaterialSave =
        Service.SaveMaterial(MaterialPath, Material);
    const auto UpdatedMaterial = EngineLoop.GetAssetManager().LoadMaterial(
        MaterialPath, EngineLoop.GetAssetRegistry());
    Runner.Expect(
        MaterialSave.bSucceeded
            && UpdatedMaterial != nullptr
            && UpdatedMaterial->Roughness == 0.85f,
        "Saving Material parameters invalidates and reloads cached data");

    Pico::FAssetPath RenamedTexturePath;
    Pico::FAssetPath::TryParse(
        "/Game/Textures/ColorsRenamed.ptex", RenamedTexturePath);
    const Pico::FEditorAssetResult TextureRename =
        Service.RenameAsset(TexturePath, "ColorsRenamed");
    const auto MaterialAfterTextureRename = EngineLoop.GetAssetManager().LoadMaterial(
        MaterialPath, EngineLoop.GetAssetRegistry());
    Runner.Expect(
        TextureRename.bSucceeded
            && TextureRename.AssetPath == RenamedTexturePath
            && EngineLoop.GetAssetRegistry().Find(TexturePath) == nullptr
            && EngineLoop.GetAssetRegistry().Find(RenamedTexturePath) != nullptr
            && MaterialAfterTextureRename != nullptr
            && MaterialAfterTextureRename->BaseColorTexture == RenamedTexturePath,
        "Renaming a Texture moves the asset and rewrites Material references");
    TexturePath = RenamedTexturePath;

    Pico::FAssetPath RenamedMaterialPath;
    Pico::FAssetPath::TryParse(
        "/Game/Materials/TestMaterialRenamed.pmat", RenamedMaterialPath);
    const Pico::FEditorAssetResult MaterialRename =
        Service.RenameAsset(MaterialPath, "TestMaterialRenamed");
    const Pico::FEditorCommandResult ReplaceMaterialReferences =
        Commands.ReplaceAssetReferences(
            MaterialPath,
            RenamedMaterialPath,
            Pico::EAssetType::Material);
    Runner.Expect(
        MaterialRename.bSucceeded
            && MaterialRename.AssetPath == RenamedMaterialPath
            && ReplaceMaterialReferences.bSucceeded
            && !Transactions.CanUndo()
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetMaterialAsset() == RenamedMaterialPath,
        "Renaming a Material updates scene references and invalidates stale Undo history");
    MaterialPath = RenamedMaterialPath;

    const Pico::FAssetRecord* TextureRecordBeforeDelete =
        EngineLoop.GetAssetRegistry().Find(TexturePath);
    const Pico::FAssetRecord* MaterialRecordBeforeDelete =
        EngineLoop.GetAssetRegistry().Find(MaterialPath);
    const std::filesystem::path TextureFile = TextureRecordBeforeDelete != nullptr
        ? TextureRecordBeforeDelete->FilePath : std::filesystem::path {};
    const std::filesystem::path MaterialFile = MaterialRecordBeforeDelete != nullptr
        ? MaterialRecordBeforeDelete->FilePath : std::filesystem::path {};
    Pico::FStagedAssetDeletion TextureRollbackDeletion;
    const Pico::FEditorAssetResult StageTextureForRollback = Service.StageDeleteAssets(
        {TexturePath}, false, TextureRollbackDeletion);
    const Pico::FEditorAssetResult ClearMaterialTextureReference =
        Service.ClearStagedAssetReferences(TextureRollbackDeletion);
    Pico::FMaterialData MaterialWithClearedTexture;
    const bool bMaterialReferenceWasCleared =
        Pico::LoadMaterialFromFile(MaterialFile, MaterialWithClearedTexture)
        && MaterialWithClearedTexture.BaseColorTexture.IsEmpty();
    const Pico::FEditorAssetResult TextureDeleteRollback =
        Service.RollbackStagedDelete(TextureRollbackDeletion);
    Pico::FMaterialData RestoredMaterial;
    Runner.Expect(
        StageTextureForRollback.bSucceeded
            && ClearMaterialTextureReference.bSucceeded
            && bMaterialReferenceWasCleared
            && TextureDeleteRollback.bSucceeded
            && EngineLoop.GetAssetRegistry().Find(TexturePath) != nullptr
            && Pico::LoadMaterialFromFile(MaterialFile, RestoredMaterial)
            && RestoredMaterial.BaseColorTexture == TexturePath,
        "Texture deletion rollback restores the Texture and Material reference");

    Pico::FStagedAssetDeletion MixedDeletion;
    const Pico::FEditorAssetResult StageMixedDelete = Service.StageDeleteAssets(
        {TexturePath, MaterialPath}, false, MixedDeletion);
    const Pico::FEditorAssetResult ClearMixedAssetReferences =
        Service.ClearStagedAssetReferences(MixedDeletion);
    const Pico::FEditorCommandResult ClearMixedSceneReferences =
        Commands.ClearAssetReferences(MixedDeletion.AssetPaths);
    const Pico::FEditorAssetResult CommitMixedDelete =
        Service.CommitStagedDelete(MixedDeletion);
    Runner.Expect(
        StageMixedDelete.bSucceeded
            && ClearMixedAssetReferences.bSucceeded
            && ClearMixedSceneReferences.bSucceeded
            && CommitMixedDelete.bSucceeded
            && !std::filesystem::exists(TextureFile)
            && !std::filesystem::exists(MaterialFile)
            && EngineLoop.GetAssetRegistry().Find(TexturePath) == nullptr
            && EngineLoop.GetAssetRegistry().Find(MaterialPath) == nullptr
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetMaterialAsset().IsEmpty(),
        "Mixed Texture and Material deletion clears scene references and commits together");

    Runner.Expect(
        Commands.ClearStaticMeshAssetReferences(AssetPath).bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetStaticMeshAsset().IsEmpty(),
        "Asset reference cleanup clears every matching scene component");
    Runner.Expect(
        Commands.Undo().bSucceeded
            && ObjectSelection.Resolve() != nullptr
            && static_cast<Pico::PStaticMeshComponent*>(ObjectSelection.Resolve())
                ->GetStaticMeshAsset() == AssetPath,
        "Asset reference cleanup is a reversible scene transaction");

    Pico::FStaticMeshImportOptions UpdatedOptions;
    UpdatedOptions.UniformScale = 2.0f;
    const Pico::FEditorAssetResult OptionsReimport =
        Service.ReimportStaticMeshWithOptions(AssetPath, UpdatedOptions);
    std::filesystem::path StoredSource;
    Pico::FStaticMeshImportOptions StoredOptions;
    const auto OptionsMesh = EngineLoop.GetAssetManager().LoadStaticMesh(
        AssetPath, EngineLoop.GetAssetRegistry());
    Runner.Expect(
        OptionsReimport.bSucceeded
            && Service.GetStaticMeshImportSettings(
                AssetPath, StoredSource, StoredOptions)
            && StoredOptions.UniformScale == 2.0f
            && OptionsMesh != nullptr
            && OptionsMesh->Bounds.Max.Z == 6.0f,
        "Reimport With Options rebuilds the mesh and persists updated settings");

    const std::vector<char> GoodBytes = ReadBytes(ImportedFile);
    WriteText(SourceFile, "this is not an obj\n");
    const Pico::FEditorAssetResult FailedReimport = Service.ReimportStaticMesh(AssetPath);
    Runner.Expect(
        !FailedReimport.bSucceeded && ReadBytes(ImportedFile) == GoodBytes,
        "A failed reimport preserves the previous native asset bytes");

    const Pico::FEditorAssetResult DeleteKeepSource =
        Service.DeleteStaticMesh(AssetPath, false);
    Runner.Expect(
        DeleteKeepSource.bSucceeded
            && !std::filesystem::exists(ImportedFile)
            && !std::filesystem::exists(MetadataFile)
            && std::filesystem::is_regular_file(SourceFile)
            && !Selection.Validate(EngineLoop.GetAssetRegistry())
            && !Selection.GetSelectedPath().IsValid(),
        "Asset deletion removes generated files, preserves optional source and refreshes selection");

    Pico::FAssetPath CompleteDeletePath;
    Pico::FAssetPath::TryParse(
        "/Game/Meshes/CompleteDelete.pmesh", CompleteDeletePath);
    const Pico::FEditorAssetResult SecondImport =
        Service.ImportStaticMesh(ExternalObj, CompleteDeletePath);
    const std::filesystem::path SecondSource =
        Root / "Content" / "Source" / "Meshes" / "CompleteDelete.obj";
    const Pico::FEditorAssetResult CompleteDelete =
        Service.DeleteStaticMesh(CompleteDeletePath, true);
    Runner.Expect(
        SecondImport.bSucceeded
            && CompleteDelete.bSucceeded
            && !std::filesystem::exists(SecondSource)
            && EngineLoop.GetAssetRegistry().Find(CompleteDeletePath) == nullptr,
        "Complete asset deletion also removes the project-local source when requested");

    Pico::FAssetPath BatchPathA;
    Pico::FAssetPath BatchPathB;
    Pico::FAssetPath::TryParse("/Game/Meshes/BatchA.pmesh", BatchPathA);
    Pico::FAssetPath::TryParse("/Game/Meshes/BatchB.pmesh", BatchPathB);
    const Pico::FEditorAssetResult BatchImportA =
        Service.ImportStaticMesh(ExternalObj, BatchPathA);
    const Pico::FEditorAssetResult BatchImportB =
        Service.ImportStaticMesh(ExternalObj, BatchPathB);
    Pico::FEditorAssetSelection BatchSelection;
    BatchSelection.Select(BatchPathA);
    BatchSelection.Toggle(BatchPathB);
    Runner.Expect(
        BatchImportA.bSucceeded
            && BatchImportB.bSucceeded
            && BatchSelection.Num() == 2
            && BatchSelection.Contains(BatchPathA)
            && BatchSelection.Contains(BatchPathB),
        "Asset selection supports Ctrl-style toggle selection");

    Pico::FAssetPath RangePath;
    Pico::FAssetPath::TryParse("/Game/Meshes/Range.pmesh", RangePath);
    BatchSelection.Select(BatchPathA);
    BatchSelection.SetRange(
        {BatchPathA, RangePath, BatchPathB}, BatchPathB, false);
    Runner.Expect(
        BatchSelection.Num() == 3
            && BatchSelection.Contains(RangePath)
            && BatchSelection.GetSelectedPath() == BatchPathB,
        "Asset selection supports Shift-style contiguous range selection");

    Pico::PActor* BatchActorA =
        EngineLoop.GetWorld()->SpawnActor<Pico::PActor>("BatchMeshActorA");
    Pico::PActor* BatchActorB =
        EngineLoop.GetWorld()->SpawnActor<Pico::PActor>("BatchMeshActorB");
    Pico::PStaticMeshComponent* BatchComponentA = BatchActorA != nullptr
        ? BatchActorA->CreateComponent<Pico::PStaticMeshComponent>("Mesh") : nullptr;
    Pico::PStaticMeshComponent* BatchComponentB = BatchActorB != nullptr
        ? BatchActorB->CreateComponent<Pico::PStaticMeshComponent>("Mesh") : nullptr;
    if (BatchComponentA != nullptr)
    {
        BatchComponentA->SetStaticMeshAsset(BatchPathA);
    }
    if (BatchComponentB != nullptr)
    {
        BatchComponentB->SetStaticMeshAsset(BatchPathB);
    }
    const Pico::FEditorCommandResult BatchClear =
        Commands.ClearStaticMeshAssetReferences({BatchPathA, BatchPathB});
    Runner.Expect(
        BatchClear.bSucceeded
            && BatchComponentA != nullptr
            && BatchComponentB != nullptr
            && BatchComponentA->GetStaticMeshAsset().IsEmpty()
            && BatchComponentB->GetStaticMeshAsset().IsEmpty(),
        "Batch reference cleanup clears components for every selected asset");

    const std::filesystem::path BatchFileA =
        Root / "Content" / "Meshes" / "BatchA.pmesh";
    const std::filesystem::path BatchFileB =
        Root / "Content" / "Meshes" / "BatchB.pmesh";
    const std::filesystem::path BatchSourceA =
        Root / "Content" / "Source" / "Meshes" / "BatchA.obj";
    const std::filesystem::path BatchSourceB =
        Root / "Content" / "Source" / "Meshes" / "BatchB.obj";
    Pico::FStagedAssetDeletion StagedDeletion;
    const Pico::FEditorAssetResult StageDelete = Service.StageDeleteStaticMeshes(
        {BatchPathA, BatchPathB}, false, StagedDeletion);
    const bool bStagedFilesHidden = StageDelete.bSucceeded
        && !std::filesystem::exists(BatchFileA)
        && !std::filesystem::exists(BatchFileB)
        && EngineLoop.GetAssetRegistry().Find(BatchPathA) == nullptr
        && EngineLoop.GetAssetRegistry().Find(BatchPathB) == nullptr;
    const Pico::FEditorAssetResult RollbackDelete =
        Service.RollbackStagedDelete(StagedDeletion);
    Runner.Expect(
        bStagedFilesHidden
            && RollbackDelete.bSucceeded
            && std::filesystem::is_regular_file(BatchFileA)
            && std::filesystem::is_regular_file(BatchFileB)
            && EngineLoop.GetAssetRegistry().Find(BatchPathA) != nullptr
            && EngineLoop.GetAssetRegistry().Find(BatchPathB) != nullptr,
        "Staged batch deletion restores every file and Registry record on rollback");

    const Pico::FEditorAssetResult BatchDelete =
        Service.DeleteStaticMeshes({BatchPathA, BatchPathB}, false);
    Runner.Expect(
        BatchDelete.bSucceeded
            && !std::filesystem::exists(BatchFileA)
            && !std::filesystem::exists(BatchFileB)
            && std::filesystem::is_regular_file(BatchSourceA)
            && std::filesystem::is_regular_file(BatchSourceB)
            && EngineLoop.GetAssetRegistry().Find(BatchPathA) == nullptr
            && EngineLoop.GetAssetRegistry().Find(BatchPathB) == nullptr,
        "Batch deletion removes all selected assets in one Registry refresh");
    Runner.Expect(
        !BatchSelection.Validate(EngineLoop.GetAssetRegistry())
            && BatchSelection.Num() == 0,
        "Asset selection drops every path removed by batch deletion");

    std::error_code Error;
    EngineLoop.Exit();
    std::filesystem::remove_all(Root, Error);
    Runner.Expect(
        Pico::FObjectRegistry::GetObjectCount() == 0,
        "Editor asset tests release their EngineLoop objects");
}
}

int main()
{
    FTestRunner Runner;
    TestEditorAssetWorkflow(Runner);
    return Runner.Finish();
}
