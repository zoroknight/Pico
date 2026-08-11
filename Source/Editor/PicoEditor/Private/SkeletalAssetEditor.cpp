#include "SkeletalAssetEditor.h"
#include "NativeFileDialog.h"

#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/SkeletalAnimation.h"
#include "Pico/AssetImport/SkeletalAnimationImporter.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/AnimInstance.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/SkeletalMeshComponent.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
FOpenGLProcedure LoadOpenGLProcedure(const char* Name)
{
    return reinterpret_cast<FOpenGLProcedure>(glfwGetProcAddress(Name));
}

std::string SanitizeName(std::string_view Value, std::string_view Fallback)
{
    std::string Result;
    Result.reserve(Value.size());
    for (char Character : Value)
    {
        const unsigned char Code = static_cast<unsigned char>(Character);
        Result.push_back(std::isalnum(Code) || Character == '_' ? Character : '_');
    }
    while (!Result.empty() && Result.back() == '_') Result.pop_back();
    return Result.empty() ? std::string(Fallback) : Result;
}

void CopyToBuffer(std::string_view Value, std::span<char> Buffer)
{
    std::fill(Buffer.begin(), Buffer.end(), '\0');
    const std::size_t Count = std::min(Value.size(), Buffer.size() - 1);
    std::copy_n(Value.data(), Count, Buffer.data());
}

bool MakeAssetPath(
    std::string_view Folder,
    std::string_view Name,
    std::string_view Extension,
    FAssetPath& OutPath)
{
    std::string Value(Folder);
    while (!Value.empty() && Value.back() == '/') Value.pop_back();
    return Value.starts_with("/Game")
        && FAssetPath::TryParse(Value + "/" + std::string(Name) + std::string(Extension), OutPath);
}

bool ResolveContentPath(const FAssetPath& AssetPath, std::filesystem::path& OutPath)
{
    return AssetPath.IsValid()
        && FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Content,
            std::filesystem::path(AssetPath.GetGameRelativePath()),
            OutPath);
}
}

struct FSkeletalAssetEditor::FImpl
{
    FEngineLoop* EngineLoop = nullptr;
    FStatus SetStatus;
    FImported OnImported;
    std::unique_ptr<FSceneViewportRenderer> Renderer;
    PWorld* PreviewWorld = nullptr;
    PSkeletalMeshComponent* PreviewComponent = nullptr;
    std::shared_ptr<const FSkeletonData> Skeleton;
    std::shared_ptr<const FSkeletalMeshData> Mesh;
    std::vector<std::shared_ptr<const FAnimationClipData>> Clips;
    std::vector<std::string> ClipLabels;
    FSkeletalImportResult PendingImport;
    std::filesystem::path SourceFile;
    FAssetPath OpenedAssetPath;
    std::array<char, 256> DestinationFolder {};
    std::array<char, 128> AssetName {};
    FVector3 ViewTarget = FVector3::ZeroVector;
    float ViewDistance = 300.0f;
    float ViewYaw = -135.0f;
    float ViewPitch = -20.0f;
    float PlaybackSpeed = 1.0f;
    int SelectedClip = 0;
    bool bOpen = false;
    bool bPlaying = true;
    bool bReferencePose = false;
    bool bHasPendingImport = false;

    ~FImpl()
    {
        ResetPreviewWorld();
        if (Renderer != nullptr) Renderer->Shutdown();
    }

    void Report(std::string Message, bool bError = false) const
    {
        if (SetStatus) SetStatus(std::move(Message), bError);
    }

    bool EnsureRenderer()
    {
        if (Renderer != nullptr) return Renderer->IsInitialized();
        Renderer = std::make_unique<FSceneViewportRenderer>();
        if (!Renderer->Initialize(&LoadOpenGLProcedure))
        {
            Renderer.reset();
            Report("Could not initialize the skeletal preview renderer", true);
            return false;
        }
        return true;
    }

    void ResetPreviewWorld()
    {
        PreviewComponent = nullptr;
        if (PreviewWorld != nullptr)
        {
            RemoveFromRoot(PreviewWorld);
            DestroyObjectTree(PreviewWorld);
            PreviewWorld = nullptr;
        }
    }

    bool BuildPreviewWorld()
    {
        ResetPreviewWorld();
        if (EngineLoop == nullptr || Skeleton == nullptr || Mesh == nullptr) return false;
        PreviewWorld = NewObject<PWorld>(
            nullptr, "SkeletalAssetPreviewWorld", EObjectFlags::Transient);
        if (PreviewWorld == nullptr
            || !AddToRoot(PreviewWorld)
            || !PreviewWorld->Initialize())
        {
            ResetPreviewWorld();
            return false;
        }
        PreviewWorld->SetAssetServices(
            &EngineLoop->GetAssetRegistry(), &EngineLoop->GetAssetManager());
        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Name = FName("PreviewActor");
        SpawnParameters.ObjectFlags = EObjectFlags::Transient;
        PActor* Actor = PreviewWorld->SpawnActor<PActor>(SpawnParameters);
        PreviewComponent = Actor != nullptr
            ? Actor->CreateComponent<PSkeletalMeshComponent>("PreviewSkeletalMesh")
            : nullptr;
        if (Actor == nullptr || PreviewComponent == nullptr
            || !Actor->SetRootComponent(PreviewComponent))
        {
            ResetPreviewWorld();
            return false;
        }
        ApplySelectedAnimation();
        PreviewWorld->Tick(0.0001f);
        FocusMesh();
        return true;
    }

    void FocusMesh()
    {
        if (Mesh == nullptr) return;
        ViewTarget = (Mesh->Bounds.Min + Mesh->Bounds.Max) * 0.5f;
        const float Radius = std::max((Mesh->Bounds.Max - Mesh->Bounds.Min).Size() * 0.5f, 1.0f);
        ViewDistance = std::clamp(Radius * 2.6f, 10.0f, 100000.0f);
        ViewYaw = -135.0f;
        ViewPitch = -20.0f;
    }

    void ApplySelectedAnimation()
    {
        if (PreviewComponent == nullptr) return;
        std::shared_ptr<const FAnimationClipData> Clip;
        if (!bReferencePose && SelectedClip >= 0
            && static_cast<std::size_t>(SelectedClip) < Clips.size())
        {
            Clip = Clips[static_cast<std::size_t>(SelectedClip)];
        }
        PreviewComponent->SetRuntimeAnimationSet(Skeleton, Mesh, Clip, Clip, Clip);
    }

    void SetPreviewData(
        std::shared_ptr<const FSkeletonData> InSkeleton,
        std::shared_ptr<const FSkeletalMeshData> InMesh,
        std::vector<std::shared_ptr<const FAnimationClipData>> InClips,
        std::vector<std::string> InLabels)
    {
        Skeleton = std::move(InSkeleton);
        Mesh = std::move(InMesh);
        Clips = std::move(InClips);
        ClipLabels = std::move(InLabels);
        SelectedClip = 0;
        bReferencePose = Clips.empty();
        bPlaying = true;
        bOpen = true;
        if (!BuildPreviewWorld())
        {
            Report("Could not create the transient skeletal preview World", true);
        }
    }

    void OpenExternal(const std::filesystem::path& File)
    {
        const std::string Name = SanitizeName(File.stem().string(), "SkeletalMesh");
        FAssetPath PreviewSkeletonPath;
        if (!FAssetPath::TryParse(
                "/Game/__Preview/" + Name + ".pskeleton", PreviewSkeletonPath))
        {
            Report("Could not create a temporary Skeleton path", true);
            return;
        }
        FSkeletalImportResult Result;
        ESkeletalImportError Error = ESkeletalImportError::None;
        if (!ImportSkeletalAnimation(File, PreviewSkeletonPath, {}, Result, &Error))
        {
            Report("Skeletal import failed: " + std::string(ToString(Error)), true);
            return;
        }
        PendingImport = Result;
        SourceFile = File;
        OpenedAssetPath = {};
        bHasPendingImport = true;
        CopyToBuffer("/Game/Characters/" + Name, DestinationFolder);
        CopyToBuffer(Name, AssetName);

        std::vector<std::shared_ptr<const FAnimationClipData>> PreviewClips;
        std::vector<std::string> Labels;
        for (const FAnimationClipData& Clip : PendingImport.Animations)
        {
            PreviewClips.push_back(std::make_shared<FAnimationClipData>(Clip));
            Labels.push_back(Clip.Name);
        }
        SetPreviewData(
            std::make_shared<FSkeletonData>(PendingImport.Skeleton),
            std::make_shared<FSkeletalMeshData>(PendingImport.Mesh),
            std::move(PreviewClips),
            std::move(Labels));
        Report("Quick Preview loaded " + File.filename().string());
    }

    void OpenRegisteredAsset(const FAssetPath& AssetPath)
    {
        if (EngineLoop == nullptr) return;
        FAssetRegistry& Registry = EngineLoop->GetAssetRegistry();
        FAssetManager& Manager = EngineLoop->GetAssetManager();
        const FAssetRecord* Record = Registry.Find(AssetPath);
        if (Record == nullptr) return;

        std::shared_ptr<const FSkeletalMeshData> LoadedMesh;
        std::shared_ptr<const FAnimationClipData> RequestedClip;
        FAssetPath SkeletonPath;
        if (Record->Type == EAssetType::SkeletalMesh)
        {
            LoadedMesh = Manager.LoadSkeletalMesh(AssetPath, Registry);
            if (LoadedMesh != nullptr) SkeletonPath = LoadedMesh->SkeletonAsset;
        }
        else if (Record->Type == EAssetType::AnimationClip)
        {
            RequestedClip = Manager.LoadAnimationClip(AssetPath, Registry);
            if (RequestedClip != nullptr) SkeletonPath = RequestedClip->SkeletonAsset;
            for (const FAssetRecord& Candidate : Registry.GetAssets())
            {
                if (Candidate.Type != EAssetType::SkeletalMesh) continue;
                const auto CandidateMesh = Manager.LoadSkeletalMesh(Candidate.AssetPath, Registry);
                if (CandidateMesh != nullptr && CandidateMesh->SkeletonAsset == SkeletonPath)
                {
                    LoadedMesh = CandidateMesh;
                    break;
                }
            }
        }
        if (LoadedMesh == nullptr || !SkeletonPath.IsValid())
        {
            Report("No compatible Skeletal Mesh is available for preview", true);
            return;
        }
        const auto LoadedSkeleton = Manager.LoadSkeleton(SkeletonPath, Registry);
        if (LoadedSkeleton == nullptr)
        {
            Report("Could not load the Skeleton referenced by the asset", true);
            return;
        }
        std::vector<std::shared_ptr<const FAnimationClipData>> LoadedClips;
        std::vector<std::string> Labels;
        for (const FAssetRecord& Candidate : Registry.GetAssets())
        {
            if (Candidate.Type != EAssetType::AnimationClip) continue;
            const auto Clip = Manager.LoadAnimationClip(Candidate.AssetPath, Registry);
            if (Clip != nullptr && Clip->SkeletonAsset == SkeletonPath)
            {
                if (RequestedClip != nullptr && Clip == RequestedClip)
                {
                    LoadedClips.insert(LoadedClips.begin(), Clip);
                    Labels.insert(Labels.begin(), std::string(Candidate.AssetPath.ToString()));
                }
                else
                {
                    LoadedClips.push_back(Clip);
                    Labels.push_back(std::string(Candidate.AssetPath.ToString()));
                }
            }
        }
        PendingImport = {};
        SourceFile.clear();
        OpenedAssetPath = AssetPath;
        bHasPendingImport = false;
        SetPreviewData(
            LoadedSkeleton, LoadedMesh, std::move(LoadedClips), std::move(Labels));
        Report("Opened skeletal asset preview");
    }

    bool ImportToProject()
    {
        if (!bHasPendingImport || EngineLoop == nullptr) return false;
        const std::string Folder(DestinationFolder.data());
        const std::string Name = SanitizeName(AssetName.data(), "SkeletalMesh");
        FAssetPath SkeletonPath;
        FAssetPath MeshPath;
        if (!MakeAssetPath(Folder, Name, ".pskeleton", SkeletonPath)
            || !MakeAssetPath(Folder, Name, ".pskeletalmesh", MeshPath))
        {
            Report("Destination must be a valid /Game folder and asset name", true);
            return false;
        }

        FSkeletalImportResult Result = PendingImport;
        Result.Mesh.SkeletonAsset = SkeletonPath;
        std::set<std::string> UsedAnimationNames;
        std::vector<FAssetPath> AnimationPaths;
        for (std::size_t Index = 0; Index < Result.Animations.size(); ++Index)
        {
            std::string ClipName = SanitizeName(
                Result.Animations[Index].Name,
                "Animation_" + std::to_string(Index));
            const std::string BaseName = ClipName;
            for (unsigned int Suffix = 2; !UsedAnimationNames.insert(ClipName).second; ++Suffix)
            {
                ClipName = BaseName + "_" + std::to_string(Suffix);
            }
            Result.Animations[Index].Name = ClipName;
            Result.Animations[Index].SkeletonAsset = SkeletonPath;
            FAssetPath ClipPath;
            if (!MakeAssetPath(Folder + "/Animations", ClipName, ".panimation", ClipPath))
            {
                Report("Could not create an Animation asset path", true);
                return false;
            }
            AnimationPaths.push_back(ClipPath);
        }

        std::vector<std::pair<FAssetPath, std::filesystem::path>> Outputs;
        std::filesystem::path SkeletonFile;
        std::filesystem::path MeshFile;
        if (!ResolveContentPath(SkeletonPath, SkeletonFile)
            || !ResolveContentPath(MeshPath, MeshFile))
        {
            Report("Could not resolve the destination inside project Content", true);
            return false;
        }
        Outputs.emplace_back(SkeletonPath, SkeletonFile);
        Outputs.emplace_back(MeshPath, MeshFile);
        for (const FAssetPath& ClipPath : AnimationPaths)
        {
            std::filesystem::path ClipFile;
            if (!ResolveContentPath(ClipPath, ClipFile))
            {
                Report("Could not resolve an Animation destination", true);
                return false;
            }
            Outputs.emplace_back(ClipPath, std::move(ClipFile));
        }
        std::error_code FileError;
        for (const auto& [AssetPath, FilePath] : Outputs)
        {
            if (EngineLoop->GetAssetRegistry().Find(AssetPath) != nullptr
                || std::filesystem::exists(FilePath, FileError))
            {
                Report("An asset already exists at " + std::string(AssetPath.ToString()), true);
                return false;
            }
        }

        ESkeletalAssetError AssetError = ESkeletalAssetError::None;
        bool bSaved = SaveSkeletonToFile(SkeletonFile, Result.Skeleton, &AssetError)
            && SaveSkeletalMeshToFile(MeshFile, Result.Mesh, &AssetError);
        for (std::size_t Index = 0; bSaved && Index < Result.Animations.size(); ++Index)
        {
            bSaved = SaveAnimationClipToFile(
                Outputs[Index + 2].second, Result.Animations[Index], &AssetError);
        }
        if (!bSaved)
        {
            for (const auto& Output : Outputs)
            {
                std::filesystem::remove(Output.second, FileError);
            }
            Report("Could not save all skeletal assets; partial output was removed", true);
            return false;
        }

        FAssetScanReport ScanReport;
        if (!EngineLoop->GetAssetRegistry().ScanProjectContent(&ScanReport)
            || EngineLoop->GetAssetRegistry().Find(MeshPath) == nullptr)
        {
            Report("Assets were saved, but AssetRegistry refresh failed", true);
            return false;
        }
        for (const auto& Output : Outputs)
        {
            EngineLoop->GetAssetManager().Invalidate(Output.first);
        }
        PendingImport = Result;
        OpenedAssetPath = MeshPath;
        bHasPendingImport = false;
        if (OnImported) OnImported(MeshPath);
        OpenRegisteredAsset(MeshPath);
        Report("Imported skeletal assets to " + Folder);
        return true;
    }

    void DrawControls()
    {
        if (bHasPendingImport)
        {
            ImGui::Text("Source: %s", SourceFile.filename().string().c_str());
            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputText("Destination", DestinationFolder.data(), DestinationFolder.size());
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Asset Name", AssetName.data(), AssetName.size());
            if (ImGui::Button("Import To Project")) ImportToProject();
            ImGui::SameLine();
            ImGui::TextDisabled("Quick Preview is transient until imported");
        }
        else if (OpenedAssetPath.IsValid())
        {
            ImGui::Text("Asset: %s", OpenedAssetPath.ToString().data());
        }
        ImGui::Text(
            "Bones: %zu   Vertices: %zu   Triangles: %zu   Clips: %zu",
            Skeleton != nullptr ? Skeleton->Bones.size() : 0,
            Mesh != nullptr ? Mesh->Vertices.size() : 0,
            Mesh != nullptr ? Mesh->Indices.size() / 3 : 0,
            Clips.size());

        const bool bPreviousReferencePose = bReferencePose;
        ImGui::Checkbox("Reference Pose", &bReferencePose);
        if (bPreviousReferencePose != bReferencePose) ApplySelectedAnimation();
        ImGui::SameLine();
        ImGui::BeginDisabled(bReferencePose || Clips.empty());
        if (ImGui::Button(bPlaying ? "||" : ">")) bPlaying = !bPlaying;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(bPlaying ? "Pause" : "Play");
        ImGui::SameLine();
        if (ImGui::Button("|<") && PreviewComponent != nullptr)
        {
            PreviewComponent->SetAnimationPlaybackTime(0.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart animation");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("Speed", &PlaybackSpeed, 0.1f, 2.0f, "%.1fx");
        ImGui::SameLine();
        if (ImGui::Button("Focus")) FocusMesh();
        ImGui::EndDisabled();

        if (!Clips.empty())
        {
            ImGui::SetNextItemWidth(420.0f);
            const char* Preview = ClipLabels[static_cast<std::size_t>(SelectedClip)].c_str();
            if (ImGui::BeginCombo("Animation", Preview))
            {
                for (std::size_t Index = 0; Index < ClipLabels.size(); ++Index)
                {
                    const bool bSelected = SelectedClip == static_cast<int>(Index);
                    if (ImGui::Selectable(ClipLabels[Index].c_str(), bSelected))
                    {
                        SelectedClip = static_cast<int>(Index);
                        ApplySelectedAnimation();
                    }
                }
                ImGui::EndCombo();
            }
        }

        PAnimInstance* AnimInstance = PreviewComponent != nullptr
            ? PreviewComponent->GetAnimInstance() : nullptr;
        const FAnimationClipData* CurrentClip = AnimInstance != nullptr
            ? AnimInstance->GetCurrentClip() : nullptr;
        if (CurrentClip != nullptr)
        {
            float Time = AnimInstance->GetPlaybackTime();
            if (CurrentClip->bLooping && CurrentClip->Duration > 0.0f)
            {
                Time = std::fmod(Time, CurrentClip->Duration);
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat(
                    "##AnimationTime", &Time, 0.0f, CurrentClip->Duration, "%.3f s"))
            {
                bPlaying = false;
                PreviewComponent->SetAnimationPlaybackTime(Time);
            }
        }
    }

    void DrawViewport()
    {
        if (!EnsureRenderer() || PreviewWorld == nullptr) return;
        const float DeltaSeconds = bPlaying && !bReferencePose
            ? std::clamp(ImGui::GetIO().DeltaTime * PlaybackSpeed, 0.0f, 0.1f)
            : 0.0f;
        if (DeltaSeconds > 0.0f) PreviewWorld->Tick(DeltaSeconds);

        ImVec2 Available = ImGui::GetContentRegionAvail();
        Available.x = std::max(Available.x, 128.0f);
        Available.y = std::max(Available.y, 128.0f);
        const ImGuiIO& IO = ImGui::GetIO();
        const uint32 Width = static_cast<uint32>(std::clamp(
            Available.x * IO.DisplayFramebufferScale.x, 128.0f, 4096.0f));
        const uint32 Height = static_cast<uint32>(std::clamp(
            Available.y * IO.DisplayFramebufferScale.y, 128.0f, 4096.0f));
        const float Yaw = DegreesToRadians(ViewYaw);
        const float Pitch = DegreesToRadians(ViewPitch);
        const float CosPitch = std::cos(Pitch);
        const FVector3 Direction(
            CosPitch * std::cos(Yaw),
            CosPitch * std::sin(Yaw),
            std::sin(Pitch));
        FSceneView View;
        View.Target = ViewTarget;
        View.Position = ViewTarget + Direction * ViewDistance;
        View.NearPlane = std::max(ViewDistance * 0.001f, 0.01f);
        View.FarPlane = std::max(ViewDistance * 20.0f, 1000.0f);
        if (!Renderer->Resize(Width, Height)
            || !Renderer->Render(
                PreviewWorld,
                EngineLoop->GetAssetRegistry(),
                EngineLoop->GetAssetManager(),
                View,
                {},
                false))
        {
            ImGui::TextDisabled("Preview render failed");
            return;
        }
        ImGui::Image(
            reinterpret_cast<ImTextureID>(
                static_cast<std::uintptr_t>(Renderer->GetColorTexture())),
            Available,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        if (!ImGui::IsItemHovered()) return;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            const ImVec2 Delta = ImGui::GetIO().MouseDelta;
            ViewYaw -= Delta.x * 0.35f;
            ViewPitch = std::clamp(ViewPitch + Delta.y * 0.35f, -89.0f, 89.0f);
        }
        const float Wheel = ImGui::GetIO().MouseWheel;
        if (Wheel != 0.0f)
        {
            ViewDistance = std::clamp(
                ViewDistance * std::pow(0.85f, Wheel), 1.0f, 100000.0f);
        }
    }
};

FSkeletalAssetEditor::FSkeletalAssetEditor(
    FEngineLoop* EngineLoop,
    FStatus SetStatus,
    FImported OnImported)
    : Impl(std::make_unique<FImpl>())
{
    Impl->EngineLoop = EngineLoop;
    Impl->SetStatus = std::move(SetStatus);
    Impl->OnImported = std::move(OnImported);
}

FSkeletalAssetEditor::~FSkeletalAssetEditor() = default;

void FSkeletalAssetEditor::OpenImport()
{
    const auto File = OpenSkeletalMeshFileDialog();
    if (File.has_value()) Impl->OpenExternal(*File);
}

void FSkeletalAssetEditor::OpenAsset(const FAssetPath& AssetPath)
{
    Impl->OpenRegisteredAsset(AssetPath);
}

void FSkeletalAssetEditor::Draw()
{
    if (!Impl->bOpen) return;
    ImGui::SetNextWindowSize(ImVec2(900.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Skeletal Asset Preview", &Impl->bOpen))
    {
        Impl->DrawControls();
        ImGui::Separator();
        Impl->DrawViewport();
    }
    ImGui::End();
}
}
