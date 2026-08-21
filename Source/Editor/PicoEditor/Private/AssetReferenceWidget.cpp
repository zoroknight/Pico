#include "AssetReferenceWidget.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Pico
{
namespace
{
bool MatchesType(EAssetType AssetType, EAssetReferenceType ReferenceType)
{
    switch (ReferenceType)
    {
    case EAssetReferenceType::StaticMesh:
        return AssetType == EAssetType::StaticMesh;
    case EAssetReferenceType::SkeletalMesh:
        return AssetType == EAssetType::SkeletalMesh;
    case EAssetReferenceType::AnimationClip:
        return AssetType == EAssetType::AnimationClip;
    case EAssetReferenceType::AnimationSet:
        return AssetType == EAssetType::AnimationSet;
    case EAssetReferenceType::AnimationMontage:
        return AssetType == EAssetType::AnimationMontage;
    case EAssetReferenceType::CharacterProfile:
        return AssetType == EAssetType::CharacterProfile;
    case EAssetReferenceType::ThirdPersonControlProfile:
        return AssetType == EAssetType::ThirdPersonControlProfile;
    case EAssetReferenceType::Texture:
        return AssetType == EAssetType::Texture;
    case EAssetReferenceType::Material:
        return AssetType == EAssetType::Material;
    case EAssetReferenceType::None:
        return false;
    }
    return false;
}

std::string ToLower(std::string_view Value)
{
    std::string Result(Value);
    std::transform(
        Result.begin(),
        Result.end(),
        Result.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    return Result;
}

bool IsAllowedAsset(
    const FAssetRegistry& Registry,
    const FAssetPath& AssetPath,
    EAssetReferenceType AllowedType)
{
    const FAssetRecord* Record = Registry.Find(AssetPath);
    return Record != nullptr && MatchesType(Record->Type, AllowedType);
}
}

FAssetReferenceEditResult FAssetReferenceWidget::Draw(
    const char* Id,
    const FAssetPath& CurrentValue,
    EAssetReferenceType AllowedType,
    const FAssetRegistry& Registry,
    const FAssetPath& SelectedAsset,
    bool bShowUseSelected,
    bool bShowBrowse)
{
    FAssetReferenceEditResult Result {CurrentValue};
    ImGui::PushID(Id);

    const std::string Preview = CurrentValue.IsValid()
        ? std::string(CurrentValue.ToString()) : std::string("<None>");
    const float ComboWidth = std::max(ImGui::GetContentRegionAvail().x, 240.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(ComboWidth, 0.0f),
        ImVec2(ComboWidth, 420.0f));
    if (ImGui::BeginCombo("##AssetReference", Preview.c_str()))
    {
        auto& SearchBuffer = SearchBuffers[Id];
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##AssetSearch",
            "Search assets",
            SearchBuffer.data(),
            SearchBuffer.size());
        const std::string Search = ToLower(SearchBuffer.data());

        if (ImGui::Selectable("<None>", !CurrentValue.IsValid()))
        {
            Result.Value = {};
            Result.bChanged = CurrentValue.IsValid();
        }
        for (const FAssetRecord& Record : Registry.GetAssets())
        {
            const std::string Path(Record.AssetPath.ToString());
            if (!MatchesType(Record.Type, AllowedType)
                || (!Search.empty()
                    && ToLower(Path).find(Search) == std::string::npos))
            {
                continue;
            }
            const bool bSelected = Record.AssetPath == CurrentValue;
            if (ImGui::Selectable(Path.c_str(), bSelected))
            {
                Result.Value = Record.AssetPath;
                Result.bChanged = Record.AssetPath != CurrentValue;
            }
            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* Payload =
                ImGui::AcceptDragDropPayload("PICO_ASSET_PATH"))
        {
            FAssetPath DroppedAsset;
            if (Payload->Data != nullptr
                && FAssetPath::TryParse(
                    static_cast<const char*>(Payload->Data),
                    DroppedAsset)
                && IsAllowedAsset(Registry, DroppedAsset, AllowedType))
            {
                Result.Value = DroppedAsset;
                Result.bChanged = DroppedAsset != CurrentValue;
            }
            else
            {
                Result.bRejectedDrop = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (bShowUseSelected)
    {
        const bool bCanUseSelected =
            IsAllowedAsset(Registry, SelectedAsset, AllowedType);
        ImGui::BeginDisabled(!bCanUseSelected);
        if (ImGui::SmallButton("Use Selected"))
        {
            Result.Value = SelectedAsset;
            Result.bChanged = SelectedAsset != CurrentValue;
        }
        ImGui::EndDisabled();
    }
    if (bShowBrowse)
    {
        if (bShowUseSelected)
        {
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(!CurrentValue.IsValid());
        Result.bBrowseRequested = ImGui::SmallButton("Browse");
        ImGui::EndDisabled();
    }
    if (bShowUseSelected || bShowBrowse)
    {
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!CurrentValue.IsValid());
    if (ImGui::SmallButton("Clear"))
    {
        Result.Value = {};
        Result.bChanged = true;
    }
    ImGui::EndDisabled();

    ImGui::PopID();
    return Result;
}
}
