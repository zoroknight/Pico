#include "MaterialDialog.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <utility>

namespace Pico
{
namespace
{
void DrawColorParameter(const char* Label, FVector3& Value)
{
    ImGui::PushID(Label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Label);
    ImGui::SameLine(140.0f);
    ImGui::ColorEdit3(
        "##Picker",
        &Value.X,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
    constexpr float ComponentWidth = 82.0f;
    float* Components[] = {&Value.X, &Value.Y, &Value.Z};
    for (int ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
    {
        ImGui::SameLine();
        ImGui::PushID(ComponentIndex);
        ImGui::SetNextItemWidth(ComponentWidth);
        ImGui::DragFloat(
            "##Value",
            Components[ComponentIndex],
            0.01f,
            0.0f,
            1.0f,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
    }
    ImGui::PopID();
}

void DrawScalarParameter(
    const char* Label,
    float& Value,
    float Minimum,
    float Maximum)
{
    ImGui::PushID(Label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Label);
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(280.0f);
    ImGui::DragFloat(
        "##Value",
        &Value,
        0.01f,
        Minimum,
        Maximum,
        "%.3f",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopID();
}
}

void FMaterialDialog::OpenCreate(
    FAssetPath InAssetPath,
    FAssetPath InSelectedTexture,
    const FAssetRegistry& InAssetRegistry)
{
    AssetPath = std::move(InAssetPath);
    SelectedTexture = std::move(InSelectedTexture);
    AssetRegistry = &InAssetRegistry;
    Material = {};
    bCreate = true;
    bOpenPopup = true;
}

void FMaterialDialog::OpenEdit(
    FAssetPath InAssetPath,
    const FMaterialData& InMaterial,
    FAssetPath InSelectedTexture,
    const FAssetRegistry& InAssetRegistry)
{
    AssetPath = std::move(InAssetPath);
    SelectedTexture = std::move(InSelectedTexture);
    AssetRegistry = &InAssetRegistry;
    Material = InMaterial;
    bCreate = false;
    bOpenPopup = true;
}

std::optional<FMaterialSaveRequest> FMaterialDialog::Draw()
{
    if (bOpenPopup)
    {
        ImGui::OpenPopup("Material Parameters");
        bOpenPopup = false;
    }
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(560.0f, 0.0f),
        ImVec2(560.0f, FLT_MAX));
    if (!ImGui::BeginPopupModal(
            "Material Parameters", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return std::nullopt;
    }
    ImGui::Text("Asset: %s", AssetPath.ToString().data());
    ImGui::Separator();
    DrawColorParameter("Base Color", Material.BaseColor);
    DrawScalarParameter("Metallic", Material.Metallic, 0.0f, 1.0f);
    DrawScalarParameter("Roughness", Material.Roughness, 0.04f, 1.0f);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Base Color Texture");
    ImGui::SameLine(140.0f);
    if (AssetRegistry != nullptr)
    {
        ImGui::BeginGroup();
        const FAssetReferenceEditResult TextureEdit = AssetReferenceWidget.Draw(
            "BaseColorTexture",
            Material.BaseColorTexture,
            EAssetReferenceType::Texture,
            *AssetRegistry,
            SelectedTexture,
            true,
            false);
        if (TextureEdit.bChanged)
        {
            Material.BaseColorTexture = TextureEdit.Value;
        }
        ImGui::EndGroup();
    }
    else
    {
        ImGui::TextDisabled("Asset Registry unavailable");
    }
    ImGui::Separator();

    std::optional<FMaterialSaveRequest> Request;
    const bool bSubmitWithKeyboard =
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)
        || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (ImGui::Button(bCreate ? "Create (Enter)" : "Save (Enter)")
        || bSubmitWithKeyboard)
    {
        Request = FMaterialSaveRequest {AssetPath, Material, bCreate};
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
