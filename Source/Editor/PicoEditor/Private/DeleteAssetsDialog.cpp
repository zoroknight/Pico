#include "DeleteAssetsDialog.h"

#include <imgui.h>

#include <algorithm>

namespace Pico
{
void FDeleteAssetsDialog::Open(
    std::vector<FAssetPath> InAssetPaths,
    std::vector<std::string> InSceneReferences)
{
    AssetPaths = std::move(InAssetPaths);
    SceneReferences = std::move(InSceneReferences);
    bDeleteProjectSources = false;
    bOpenPopup = true;
}

std::optional<FDeleteAssetsRequest> FDeleteAssetsDialog::Draw()
{
    if (bOpenPopup)
    {
        ImGui::OpenPopup("Delete Assets");
        bOpenPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(
            "Delete Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return std::nullopt;
    }

    ImGui::Text("Assets: %zu", AssetPaths.size());
    const std::size_t AssetDisplayCount = std::min<std::size_t>(AssetPaths.size(), 8);
    for (std::size_t Index = 0; Index < AssetDisplayCount; ++Index)
    {
        ImGui::BulletText("%s", AssetPaths[Index].ToString().data());
    }
    if (AssetPaths.size() > AssetDisplayCount)
    {
        ImGui::TextDisabled("...and %zu more assets", AssetPaths.size() - AssetDisplayCount);
    }
    ImGui::Text("References: %zu", SceneReferences.size());
    const std::size_t ReferenceDisplayCount =
        std::min<std::size_t>(SceneReferences.size(), 8);
    for (std::size_t Index = 0; Index < ReferenceDisplayCount; ++Index)
    {
        ImGui::BulletText("%s", SceneReferences[Index].c_str());
    }
    if (SceneReferences.size() > ReferenceDisplayCount)
    {
        ImGui::TextDisabled(
            "...and %zu more", SceneReferences.size() - ReferenceDisplayCount);
    }
    if (!SceneReferences.empty())
    {
        ImGui::TextColored(
            ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
            "Keeping references will leave missing assets");
    }
    ImGui::Checkbox("Delete project-local source files", &bDeleteProjectSources);
    ImGui::Separator();

    std::optional<FDeleteAssetsRequest> Request;
    if (SceneReferences.empty())
    {
        if (ImGui::Button("Delete Assets"))
        {
            Request = FDeleteAssetsRequest {AssetPaths, bDeleteProjectSources, false};
        }
    }
    else
    {
        if (ImGui::Button("Delete, Keep References"))
        {
            Request = FDeleteAssetsRequest {AssetPaths, bDeleteProjectSources, false};
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear References and Delete"))
        {
            Request = FDeleteAssetsRequest {AssetPaths, bDeleteProjectSources, true};
        }
    }
    if (Request.has_value())
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return Request;
}
}
