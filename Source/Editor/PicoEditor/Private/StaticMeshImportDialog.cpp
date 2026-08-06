#include "StaticMeshImportDialog.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Pico
{
namespace
{
FVector3 GetImportedSize(
    const FStaticMeshSourceAnalysis& Analysis,
    bool bConvertYUpToZUp)
{
    const FVector3 SourceSize = Analysis.Bounds.Max - Analysis.Bounds.Min;
    return bConvertYUpToZUp
        ? FVector3(SourceSize.X, SourceSize.Z, SourceSize.Y)
        : SourceSize;
}

float GetLargestAxis(const FVector3& Size)
{
    return std::max({std::abs(Size.X), std::abs(Size.Y), std::abs(Size.Z)});
}
}

void FStaticMeshImportDialog::Open(
    std::filesystem::path InSourceFile,
    FAssetPath InReimportAsset,
    const FStaticMeshSourceAnalysis& InAnalysis,
    const FStaticMeshImportOptions& InOptions,
    bool bUseAutomaticScale)
{
    SourceFile = std::move(InSourceFile);
    ReimportAsset = std::move(InReimportAsset);
    Analysis = InAnalysis;
    Options = InOptions;
    NormalizeTargetSize = 100.0f;
    ScalePreset = bUseAutomaticScale ? 0 : 5;
    if (bUseAutomaticScale)
    {
        ApplyScalePreset();
    }
    bOpenPopup = true;
}

std::optional<FStaticMeshImportRequest> FStaticMeshImportDialog::Draw()
{
    if (bOpenPopup)
    {
        ImGui::OpenPopup("Static Mesh Import");
        bOpenPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(
            "Static Mesh Import", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return std::nullopt;
    }

    ImGui::TextWrapped("%s", SourceFile.string().c_str());
    if (ReimportAsset.IsValid())
    {
        ImGui::Text("Asset: %s", ReimportAsset.ToString().data());
    }
    ImGui::Separator();
    ImGui::Text(
        "Source: %zu vertices, %zu triangles",
        Analysis.VertexCount,
        Analysis.TriangleCount);
    const FVector3 SourceSize = Analysis.Bounds.Max - Analysis.Bounds.Min;
    ImGui::Text(
        "Source Size: %.6g x %.6g x %.6g",
        SourceSize.X,
        SourceSize.Y,
        SourceSize.Z);

    constexpr const char* ScalePresets[] = {
        "Auto", "Centimeters", "Meters", "Millimeters", "Normalize", "Custom"};
    if (ImGui::Combo(
            "Units", &ScalePreset, ScalePresets, static_cast<int>(std::size(ScalePresets))))
    {
        ApplyScalePreset();
    }
    if (ScalePreset == 4
        && ImGui::InputFloat("Target Longest Axis", &NormalizeTargetSize, 1.0f, 10.0f))
    {
        NormalizeTargetSize = std::max(NormalizeTargetSize, 0.001f);
        ApplyScalePreset();
    }
    if (ScalePreset == 5)
    {
        ImGui::InputFloat("Uniform Scale", &Options.UniformScale, 0.1f, 10.0f);
    }
    else
    {
        ImGui::Text("Uniform Scale: %.6g", Options.UniformScale);
    }
    if (ImGui::Checkbox("Convert Y-Up to Z-Up", &Options.bConvertYUpToZUp))
    {
        ApplyScalePreset();
    }
    ImGui::Checkbox("Flip Texture V", &Options.bFlipTexCoordV);

    const FVector3 FinalSize =
        GetImportedSize(Analysis, Options.bConvertYUpToZUp) * Options.UniformScale;
    const float FinalLargestAxis = GetLargestAxis(FinalSize);
    ImGui::Text(
        "Final Size: %.3f x %.3f x %.3f", FinalSize.X, FinalSize.Y, FinalSize.Z);
    if (FinalLargestAxis < 1.0f)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Final mesh is very small");
    }
    else if (FinalLargestAxis > 100000.0f)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Final mesh is very large");
    }

    std::optional<FStaticMeshImportRequest> Request;
    const bool bValidScale = std::isfinite(Options.UniformScale) && Options.UniformScale > 0.0f;
    ImGui::BeginDisabled(!bValidScale);
    if (ImGui::Button(ReimportAsset.IsValid() ? "Reimport" : "Import"))
    {
        Request = FStaticMeshImportRequest {SourceFile, ReimportAsset, Options};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return Request;
}

void FStaticMeshImportDialog::ApplyScalePreset()
{
    const float LargestAxis = GetLargestAxis(GetImportedSize(Analysis, Options.bConvertYUpToZUp));
    switch (ScalePreset)
    {
    case 0:
        Options.UniformScale = LargestAxis < 1.0f
            ? NormalizeTargetSize / std::max(LargestAxis, 0.000001f)
            : LargestAxis <= 10.0f ? 100.0f : 1.0f;
        break;
    case 1:
        Options.UniformScale = 1.0f;
        break;
    case 2:
        Options.UniformScale = 100.0f;
        break;
    case 3:
        Options.UniformScale = 0.1f;
        break;
    case 4:
        Options.UniformScale = NormalizeTargetSize / std::max(LargestAxis, 0.000001f);
        break;
    default:
        break;
    }
}
}
