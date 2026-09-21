#include "ContentBrowserPanel.h"

#include "Pico/Asset/Material.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Asset/Texture.h"

#include <glad/gl.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace Pico
{
namespace
{
std::string ToLower(std::string_view Value)
{
    std::string Result(Value);
    std::transform(Result.begin(), Result.end(), Result.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    return Result;
}

std::string GetFolder(const FAssetPath& AssetPath)
{
    std::filesystem::path Relative(AssetPath.GetGameRelativePath());
    const std::string Folder = Relative.parent_path().generic_string();
    return Folder == "." ? std::string {} : Folder;
}

std::string FormatFileSize(std::uintmax_t Bytes)
{
    if (Bytes < 1024)
    {
        return std::to_string(Bytes) + " B";
    }
    std::ostringstream Stream;
    Stream << std::fixed << std::setprecision(Bytes < 1024 * 1024 ? 1 : 2)
        << (Bytes < 1024 * 1024
            ? static_cast<double>(Bytes) / 1024.0
            : static_cast<double>(Bytes) / (1024.0 * 1024.0))
        << (Bytes < 1024 * 1024 ? " KB" : " MB");
    return Stream.str();
}

std::string FormatWriteTime(std::filesystem::file_time_type Time)
{
    const auto SystemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        Time - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now());
    const std::time_t Value = std::chrono::system_clock::to_time_t(SystemTime);
    std::tm Local {};
#if defined(_WIN32)
    localtime_s(&Local, &Value);
#else
    localtime_r(&Value, &Local);
#endif
    std::ostringstream Stream;
    Stream << std::put_time(&Local, "%Y-%m-%d %H:%M");
    return Stream.str();
}

struct FFolderNode
{
    std::string Name;
    std::string Path;
    std::map<std::string, std::unique_ptr<FFolderNode>> Children;
};

const char* GetAssetBadge(EAssetType Type)
{
    switch (Type)
    {
    case EAssetType::World: return "WORLD";
    case EAssetType::StaticMesh: return "MESH";
    case EAssetType::Texture: return "TEX";
    case EAssetType::Material: return "MAT";
    case EAssetType::Skeleton: return "SKEL";
    case EAssetType::SkeletalMesh: return "SK MESH";
    case EAssetType::AnimationClip: return "ANIM";
    case EAssetType::AnimationSet: return "ANIM SET";
    case EAssetType::AnimationMontage: return "MONTAGE";
    case EAssetType::CharacterProfile: return "CHAR";
    case EAssetType::ThirdPersonControlProfile: return "CONTROL";
    case EAssetType::ActorBlueprint: return "BLUEPRINT";
    case EAssetType::PicoGraph: return "GRAPH";
    }
    return "ASSET";
}

ImU32 GetAssetColor(EAssetType Type, int Alpha = 255)
{
    switch (Type)
    {
    case EAssetType::World: return IM_COL32(66, 143, 102, Alpha);
    case EAssetType::StaticMesh: return IM_COL32(65, 124, 158, Alpha);
    case EAssetType::Texture: return IM_COL32(157, 111, 69, Alpha);
    case EAssetType::Material: return IM_COL32(140, 92, 153, Alpha);
    case EAssetType::Skeleton:
    case EAssetType::SkeletalMesh: return IM_COL32(65, 146, 139, Alpha);
    case EAssetType::AnimationClip:
    case EAssetType::AnimationSet:
    case EAssetType::AnimationMontage: return IM_COL32(179, 130, 54, Alpha);
    case EAssetType::CharacterProfile:
    case EAssetType::ThirdPersonControlProfile: return IM_COL32(112, 121, 141, Alpha);
    case EAssetType::ActorBlueprint: return IM_COL32(55, 119, 185, Alpha);
    case EAssetType::PicoGraph: return IM_COL32(62, 151, 123, Alpha);
    }
    return IM_COL32(90, 96, 104, Alpha);
}

void HandleAssetInteraction(
    const FAssetRecord& Record,
    const std::vector<FAssetPath>& OrderedAssets,
    FEditorAssetSelection& Selection,
    const FContentBrowserPanel::FAssetAction& Reimport,
    const FContentBrowserPanel::FAssetAction& ReimportWithOptions,
    const FContentBrowserPanel::FAssetsAction& DeleteAssets,
    const FContentBrowserPanel::FAssetAction& RenameAsset,
    const FContentBrowserPanel::FAssetAction& EditMaterial,
    const FContentBrowserPanel::FAssetAction& OpenSkeletal,
    const FContentBrowserPanel::FAssetAction& OpenActorBlueprint,
    const FContentBrowserPanel::FAssetAction& OpenPicoGraph,
    const FContentBrowserPanel::FAssetAction& OpenWorld,
    const FContentBrowserPanel::FAssetAction& Create,
    const FContentBrowserPanel::FAssetAction& Assign,
    const FContentBrowserPanel::FCanReimport& CanReimport)
{
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        const ImGuiIO& IO = ImGui::GetIO();
        if (IO.KeyShift)
        {
            Selection.SetRange(OrderedAssets, Record.AssetPath, IO.KeyCtrl);
        }
        else if (IO.KeyCtrl)
        {
            Selection.Toggle(Record.AssetPath);
        }
        else
        {
            Selection.Select(Record.AssetPath);
        }
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        if (Record.Type == EAssetType::World) OpenWorld(Record.AssetPath);
        else if (Record.Type == EAssetType::StaticMesh) Create(Record.AssetPath);
        else if (Record.Type == EAssetType::Material) EditMaterial(Record.AssetPath);
        else if (Record.Type == EAssetType::SkeletalMesh
            || Record.Type == EAssetType::AnimationClip
            || Record.Type == EAssetType::AnimationSet
            || Record.Type == EAssetType::AnimationMontage)
        {
            OpenSkeletal(Record.AssetPath);
        }
        else if (Record.Type == EAssetType::ActorBlueprint) OpenActorBlueprint(Record.AssetPath);
        else if (Record.Type == EAssetType::PicoGraph) OpenPicoGraph(Record.AssetPath);
    }

    if (ImGui::BeginDragDropSource())
    {
        const std::string Path(Record.AssetPath.ToString());
        ImGui::SetDragDropPayload("PICO_ASSET_PATH", Path.c_str(), Path.size() + 1);
        ImGui::TextUnformatted(Path.c_str());
        ImGui::EndDragDropSource();
    }

    if (!ImGui::BeginPopupContextItem())
    {
        return;
    }
    if (Record.Type == EAssetType::ActorBlueprint && ImGui::MenuItem("Open Actor Blueprint"))
    {
        OpenActorBlueprint(Record.AssetPath);
    }
    if (Record.Type == EAssetType::PicoGraph && ImGui::MenuItem("Open PicoGraph"))
    {
        OpenPicoGraph(Record.AssetPath);
    }
    if (Record.Type == EAssetType::World && ImGui::MenuItem("Open World"))
    {
        OpenWorld(Record.AssetPath);
    }
    if (Record.Type != EAssetType::World && ImGui::MenuItem("Rename", "F2"))
    {
        Selection.Select(Record.AssetPath);
        RenameAsset(Record.AssetPath);
    }
    if (Record.Type != EAssetType::World)
    {
        ImGui::Separator();
    }
    if (Record.Type == EAssetType::StaticMesh)
    {
        if (ImGui::MenuItem("Create Actor")) Create(Record.AssetPath);
        if (ImGui::MenuItem("Assign to Selection")) Assign(Record.AssetPath);
        const bool bCanReimport = CanReimport(Record.AssetPath);
        ImGui::BeginDisabled(!bCanReimport);
        if (ImGui::MenuItem("Reimport")) Reimport(Record.AssetPath);
        if (ImGui::MenuItem("Reimport With Options...")) ReimportWithOptions(Record.AssetPath);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Asset..."))
        {
            DeleteAssets(std::vector<FAssetPath> {Record.AssetPath});
        }
    }
    else if (Record.Type == EAssetType::Texture)
    {
        const bool bCanReimport = CanReimport(Record.AssetPath);
        ImGui::BeginDisabled(!bCanReimport);
        if (ImGui::MenuItem("Reimport")) Reimport(Record.AssetPath);
        ImGui::EndDisabled();
    }
    else if (Record.Type == EAssetType::Material)
    {
        if (ImGui::MenuItem("Edit Material...")) EditMaterial(Record.AssetPath);
        if (ImGui::MenuItem("Assign to Selection")) Assign(Record.AssetPath);
    }
    else if (Record.Type == EAssetType::SkeletalMesh
        || Record.Type == EAssetType::AnimationClip
        || Record.Type == EAssetType::AnimationSet
        || Record.Type == EAssetType::AnimationMontage)
    {
        if (ImGui::MenuItem("Open Preview")) OpenSkeletal(Record.AssetPath);
    }
    ImGui::EndPopup();
}
}

FContentBrowserPanel::~FContentBrowserPanel()
{
    ReleaseThumbnails();
}

void FContentBrowserPanel::Draw(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection,
    FAction Import,
    FAction ImportSkeletal,
    FAction ImportTexture,
    FAction CreateMaterial,
    FAction CreateActorBlueprint,
    FAction CreatePicoGraph,
    FAction Refresh,
    FAssetAction Reimport,
    FAssetAction ReimportWithOptions,
    FAssetsAction DeleteAssets,
    FAssetAction RenameAsset,
    FAssetAction EditMaterial,
    FAssetAction OpenSkeletal,
    FAssetAction OpenActorBlueprint,
    FAssetAction OpenPicoGraph,
    FAssetAction OpenWorld,
    FAssetAction Create,
    FAssetAction Assign,
    FCanReimport CanReimport)
{
    bKeyboardFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (ImGui::Button("Import OBJ"))
    {
        Import();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Skeletal"))
    {
        ImportSkeletal();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Texture"))
    {
        ImportTexture();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Material"))
    {
        CreateMaterial();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Actor Blueprint"))
    {
        CreateActorBlueprint();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Graph"))
    {
        CreatePicoGraph();
    }
    ImGui::SameLine();
    const FAssetRecord* Selected = Selection.Resolve(Registry);
    const bool bCanReimport = Selected != nullptr
        && CanReimport(Selected->AssetPath);
    const bool bCanOpenImportOptions = bCanReimport
        && Selected->Type == EAssetType::StaticMesh;
    ImGui::BeginDisabled(!bCanReimport);
    if (ImGui::Button("Reimport"))
    {
        Reimport(Selected->AssetPath);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool bCanDelete = Selection.Num() > 0
        && std::all_of(
            Selection.GetSelectedPaths().begin(),
            Selection.GetSelectedPaths().end(),
            [&Registry](const FAssetPath& Path)
            {
                const FAssetRecord* Record = Registry.Find(Path);
                return Record != nullptr && Record->Type == EAssetType::StaticMesh;
            });
    ImGui::BeginDisabled(!bCanDelete);
    if (ImGui::Button("Delete"))
    {
        DeleteAssets(Selection.GetSelectedPaths());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!bCanOpenImportOptions);
    if (ImGui::Button("Import Options"))
    {
        ReimportWithOptions(Selected->AssetPath);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        Refresh();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(210.0f);
    ImGui::InputTextWithHint("##AssetSearch", "Search assets", SearchBuffer.data(), SearchBuffer.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    constexpr const char* Types[] = {
        "All Types", "World", "Static Mesh", "Texture", "Material",
        "Skeleton", "Skeletal Mesh", "Animation", "Character Profile",
        "Control Profile", "Actor Blueprint", "PicoGraph"};
    ImGui::Combo("##AssetType", &TypeFilter, Types, static_cast<int>(std::size(Types)));
    ImGui::SameLine();
    const ImVec4 ActiveColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    const bool bListActive = ViewMode == EViewMode::List;
    if (bListActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ActiveColor);
    }
    if (ImGui::SmallButton("List"))
    {
        ViewMode = EViewMode::List;
    }
    if (bListActive)
    {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Show asset details");
    }
    ImGui::SameLine(0.0f, 2.0f);
    const bool bTilesActive = ViewMode == EViewMode::Tiles;
    if (bTilesActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ActiveColor);
    }
    if (ImGui::SmallButton("Tiles"))
    {
        ViewMode = EViewMode::Tiles;
    }
    if (bTilesActive)
    {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Show visual asset previews");
    }

    if (ImGui::BeginTable("ContentBrowserLayout", 2, ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        DrawFolderTree(Registry);
        ImGui::TableNextColumn();
        if (ViewMode == EViewMode::List)
        {
            DrawAssetTable(
                Registry,
                Selection,
                Reimport,
                ReimportWithOptions,
                DeleteAssets,
                RenameAsset,
                EditMaterial,
                OpenSkeletal,
                OpenActorBlueprint,
                OpenPicoGraph,
                OpenWorld,
                Create,
                Assign,
                CanReimport);
        }
        else
        {
            DrawAssetTiles(
                Registry,
                Selection,
                Reimport,
                ReimportWithOptions,
                DeleteAssets,
                RenameAsset,
                EditMaterial,
                OpenSkeletal,
                OpenActorBlueprint,
                OpenPicoGraph,
                OpenWorld,
                Create,
                Assign,
                CanReimport);
        }
        ImGui::EndTable();
    }
}

bool FContentBrowserPanel::IsKeyboardFocused() const
{
    return bKeyboardFocused;
}

bool FContentBrowserPanel::FocusAsset(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection,
    const FAssetPath& AssetPath)
{
    const FAssetRecord* Record = Registry.Find(AssetPath);
    if (Record == nullptr)
    {
        return false;
    }
    CurrentFolder = GetFolder(Record->AssetPath);
    TypeFilter = 0;
    SearchBuffer.fill('\0');
    Selection.Select(Record->AssetPath);
    return true;
}

void FContentBrowserPanel::SelectAllVisible(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection) const
{
    Selection.Clear();
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        if (PassesFilter(Record))
        {
            Selection.Add(Record.AssetPath);
        }
    }
}

bool FContentBrowserPanel::PassesFilter(const FAssetRecord& Record) const
{
    bool bMatchesType = true;
    switch (TypeFilter)
    {
    case 1: bMatchesType = Record.Type == EAssetType::World; break;
    case 2: bMatchesType = Record.Type == EAssetType::StaticMesh; break;
    case 3: bMatchesType = Record.Type == EAssetType::Texture; break;
    case 4: bMatchesType = Record.Type == EAssetType::Material; break;
    case 5: bMatchesType = Record.Type == EAssetType::Skeleton; break;
    case 6: bMatchesType = Record.Type == EAssetType::SkeletalMesh; break;
    case 7:
        bMatchesType = Record.Type == EAssetType::AnimationClip
            || Record.Type == EAssetType::AnimationSet
            || Record.Type == EAssetType::AnimationMontage;
        break;
    case 8: bMatchesType = Record.Type == EAssetType::CharacterProfile; break;
    case 9: bMatchesType = Record.Type == EAssetType::ThirdPersonControlProfile; break;
    case 10: bMatchesType = Record.Type == EAssetType::ActorBlueprint; break;
    case 11: bMatchesType = Record.Type == EAssetType::PicoGraph; break;
    default: break;
    }
    if (!bMatchesType) return false;
    const std::string Search = ToLower(SearchBuffer.data());
    if (!Search.empty() && ToLower(Record.AssetPath.ToString()).find(Search) == std::string::npos)
    {
        return false;
    }
    return Search.empty() ? GetFolder(Record.AssetPath) == CurrentFolder : true;
}

void FContentBrowserPanel::DrawFolderTree(const FAssetRegistry& Registry)
{
    FFolderNode Root {"/Game", "", {}};
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        std::filesystem::path Folder(GetFolder(Record.AssetPath));
        std::filesystem::path Accumulated;
        FFolderNode* Parent = &Root;
        for (const auto& Segment : Folder)
        {
            Accumulated /= Segment;
            const std::string Name = Segment.string();
            auto& Child = Parent->Children[Name];
            if (!Child)
            {
                Child = std::make_unique<FFolderNode>();
                Child->Name = Name;
                Child->Path = Accumulated.generic_string();
            }
            Parent = Child.get();
        }
    }

    const auto DrawNode = [this](const auto& Self, const FFolderNode& Node) -> void
    {
        const bool bLeaf = Node.Children.empty();
        ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_OpenOnDoubleClick
            | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (Node.Path.empty())
        {
            Flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (CurrentFolder == Node.Path)
        {
            Flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (bLeaf)
        {
            Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const std::string Label = Node.Name + "##Folder_" + Node.Path;
        const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            CurrentFolder = Node.Path;
        }
        if (bOpen && !bLeaf)
        {
            for (const auto& [Name, Child] : Node.Children)
            {
                (void)Name;
                Self(Self, *Child);
            }
            ImGui::TreePop();
        }
    };
    DrawNode(DrawNode, Root);
}

void FContentBrowserPanel::DrawAssetTable(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection,
    const FAssetAction& Reimport,
    const FAssetAction& ReimportWithOptions,
    const FAssetsAction& DeleteAssets,
    const FAssetAction& RenameAsset,
    const FAssetAction& EditMaterial,
    const FAssetAction& OpenSkeletal,
    const FAssetAction& OpenActorBlueprint,
    const FAssetAction& OpenPicoGraph,
    const FAssetAction& OpenWorld,
    const FAssetAction& Create,
    const FAssetAction& Assign,
    const FCanReimport& CanReimport)
{
    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("Assets", 5, Flags))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Virtual Path", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 132.0f);
    ImGui::TableHeadersRow();

    std::vector<FAssetPath> OrderedAssets;
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        if (PassesFilter(Record))
        {
            OrderedAssets.push_back(Record.AssetPath);
        }
    }
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        if (!PassesFilter(Record))
        {
            continue;
        }
        ImGui::PushID(Record.AssetPath.ToString().data());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string Name = Record.FilePath.filename().string();
        const bool bSelected = Selection.Contains(Record.AssetPath);
        ImGui::Selectable(Name.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns);
        HandleAssetInteraction(
            Record, OrderedAssets, Selection, Reimport, ReimportWithOptions,
            DeleteAssets, RenameAsset, EditMaterial, OpenSkeletal,
            OpenActorBlueprint, OpenPicoGraph, OpenWorld, Create, Assign, CanReimport);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToString(Record.Type).data());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Record.AssetPath.ToString().data());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FormatFileSize(Record.FileSize).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FormatWriteTime(Record.LastWriteTime).c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void FContentBrowserPanel::DrawAssetTiles(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection,
    const FAssetAction& Reimport,
    const FAssetAction& ReimportWithOptions,
    const FAssetsAction& DeleteAssets,
    const FAssetAction& RenameAsset,
    const FAssetAction& EditMaterial,
    const FAssetAction& OpenSkeletal,
    const FAssetAction& OpenActorBlueprint,
    const FAssetAction& OpenPicoGraph,
    const FAssetAction& OpenWorld,
    const FAssetAction& Create,
    const FAssetAction& Assign,
    const FCanReimport& CanReimport)
{
    std::vector<const FAssetRecord*> VisibleAssets;
    std::vector<FAssetPath> OrderedAssets;
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        if (PassesFilter(Record))
        {
            VisibleAssets.push_back(&Record);
            OrderedAssets.push_back(Record.AssetPath);
        }
    }

    if (!ImGui::BeginChild("AssetTiles", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::EndChild();
        return;
    }

    constexpr float TileWidth = 154.0f;
    constexpr float TileHeight = 164.0f;
    constexpr float PreviewHeight = 112.0f;
    constexpr float Spacing = 10.0f;
    const int ColumnCount = std::max(1, static_cast<int>(
        (ImGui::GetContentRegionAvail().x + Spacing) / (TileWidth + Spacing)));
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    for (std::size_t Index = 0; Index < VisibleAssets.size(); ++Index)
    {
        const FAssetRecord& Record = *VisibleAssets[Index];
        ImGui::PushID(Record.AssetPath.ToString().data());
        const ImVec2 TileMin = ImGui::GetCursorScreenPos();
        const ImVec2 TileMax(TileMin.x + TileWidth, TileMin.y + TileHeight);
        ImGui::InvisibleButton("##AssetTile", ImVec2(TileWidth, TileHeight));
        const bool bHovered = ImGui::IsItemHovered();
        const bool bSelected = Selection.Contains(Record.AssetPath);

        const ImU32 Background = bSelected
            ? IM_COL32(39, 84, 78, 255)
            : (bHovered ? IM_COL32(49, 52, 57, 255) : IM_COL32(35, 37, 41, 255));
        const ImU32 Border = bSelected
            ? IM_COL32(70, 185, 157, 255)
            : (bHovered ? IM_COL32(104, 109, 117, 255) : IM_COL32(61, 64, 70, 255));
        DrawList->AddRectFilled(TileMin, TileMax, Background, 4.0f);
        DrawList->AddRect(TileMin, TileMax, Border, 4.0f, 0, bSelected ? 2.0f : 1.0f);

        const ImVec2 PreviewMin(TileMin.x + 6.0f, TileMin.y + 6.0f);
        const ImVec2 PreviewMax(TileMax.x - 6.0f, TileMin.y + PreviewHeight);
        DrawList->AddRectFilled(PreviewMin, PreviewMax, IM_COL32(24, 26, 29, 255), 2.0f);
        FThumbnailCacheEntry& Thumbnail = GetThumbnail(Record);
        if (Record.Type == EAssetType::Texture && Thumbnail.Texture != 0)
        {
            DrawList->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(Thumbnail.Texture)),
                PreviewMin, PreviewMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else if (Record.Type == EAssetType::Material)
        {
            const ImU32 MaterialColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                Thumbnail.Color[0], Thumbnail.Color[1], Thumbnail.Color[2], 1.0f));
            const ImVec2 Center(
                (PreviewMin.x + PreviewMax.x) * 0.5f,
                (PreviewMin.y + PreviewMax.y) * 0.5f);
            DrawList->AddCircleFilled(Center, 39.0f, MaterialColor, 32);
            DrawList->AddCircle(Center, 39.0f, IM_COL32(225, 225, 225, 110), 32, 1.5f);
        }
        else if (Record.Type == EAssetType::StaticMesh && !Thumbnail.MeshLines.empty())
        {
            for (std::size_t Line = 0; Line + 1 < Thumbnail.MeshLines.size(); Line += 2)
            {
                const auto& A = Thumbnail.MeshLines[Line];
                const auto& B = Thumbnail.MeshLines[Line + 1];
                const ImVec2 Start(
                    PreviewMin.x + A[0] * (PreviewMax.x - PreviewMin.x),
                    PreviewMin.y + A[1] * (PreviewMax.y - PreviewMin.y));
                const ImVec2 End(
                    PreviewMin.x + B[0] * (PreviewMax.x - PreviewMin.x),
                    PreviewMin.y + B[1] * (PreviewMax.y - PreviewMin.y));
                DrawList->AddLine(Start, End, IM_COL32(114, 202, 221, 205), 1.0f);
            }
        }
        else
        {
            const ImU32 AssetColor = GetAssetColor(Record.Type, 235);
            const ImVec2 Center(
                (PreviewMin.x + PreviewMax.x) * 0.5f,
                (PreviewMin.y + PreviewMax.y) * 0.5f - 5.0f);
            DrawList->AddCircleFilled(Center, 31.0f, AssetColor, 6);
            const char* Badge = GetAssetBadge(Record.Type);
            const ImVec2 TextSize = ImGui::CalcTextSize(Badge);
            DrawList->AddText(
                ImVec2(Center.x - TextSize.x * 0.5f, Center.y - TextSize.y * 0.5f),
                IM_COL32(245, 247, 249, 255), Badge);
        }

        const std::string Name = Record.FilePath.filename().string();
        const std::string Type(ToString(Record.Type));
        DrawList->PushClipRect(
            ImVec2(TileMin.x + 7.0f, PreviewMax.y + 4.0f),
            ImVec2(TileMax.x - 7.0f, TileMax.y - 4.0f), true);
        DrawList->AddText(
            ImVec2(TileMin.x + 8.0f, PreviewMax.y + 7.0f),
            IM_COL32(235, 237, 240, 255), Name.c_str());
        DrawList->AddText(
            ImVec2(TileMin.x + 8.0f, PreviewMax.y + 27.0f),
            GetAssetColor(Record.Type), Type.c_str());
        DrawList->PopClipRect();

        HandleAssetInteraction(
            Record, OrderedAssets, Selection, Reimport, ReimportWithOptions,
            DeleteAssets, RenameAsset, EditMaterial, OpenSkeletal,
            OpenActorBlueprint, OpenPicoGraph, OpenWorld, Create, Assign, CanReimport);
        if (bHovered)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(Record.AssetPath.ToString().data());
            ImGui::Text("Type: %s", Type.c_str());
            ImGui::Text("Size: %s", FormatFileSize(Record.FileSize).c_str());
            ImGui::Text("Modified: %s", FormatWriteTime(Record.LastWriteTime).c_str());
            ImGui::EndTooltip();
        }

        if ((static_cast<int>(Index) + 1) % ColumnCount != 0)
        {
            ImGui::SameLine(0.0f, Spacing);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

FContentBrowserPanel::FThumbnailCacheEntry& FContentBrowserPanel::GetThumbnail(
    const FAssetRecord& Record)
{
    const std::string Key(Record.AssetPath.ToString());
    FThumbnailCacheEntry& Entry = Thumbnails[Key];
    if (Entry.bLoaded && Entry.LastWriteTime == Record.LastWriteTime)
    {
        return Entry;
    }

    if (Entry.Texture != 0)
    {
        glDeleteTextures(1, &Entry.Texture);
        Entry.Texture = 0;
    }
    Entry.MeshLines.clear();
    Entry.Color = {0.25f, 0.28f, 0.30f};
    Entry.LastWriteTime = Record.LastWriteTime;
    Entry.bLoaded = true;

    if (Record.Type == EAssetType::Texture)
    {
        FTextureData Texture;
        if (LoadTextureFromFile(Record.FilePath, Texture)
            && Texture.Width > 0 && Texture.Height > 0 && !Texture.Pixels.empty())
        {
            glGenTextures(1, &Entry.Texture);
            glBindTexture(GL_TEXTURE_2D, Entry.Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8,
                static_cast<GLsizei>(Texture.Width), static_cast<GLsizei>(Texture.Height),
                0, GL_RGBA, GL_UNSIGNED_BYTE, Texture.Pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    else if (Record.Type == EAssetType::Material)
    {
        FMaterialData Material;
        if (LoadMaterialFromFile(Record.FilePath, Material))
        {
            Entry.Color = {
                std::clamp(Material.BaseColor.X, 0.0f, 1.0f),
                std::clamp(Material.BaseColor.Y, 0.0f, 1.0f),
                std::clamp(Material.BaseColor.Z, 0.0f, 1.0f)};
        }
    }
    else if (Record.Type == EAssetType::StaticMesh)
    {
        FStaticMeshData Mesh;
        if (LoadStaticMeshFromFile(Record.FilePath, Mesh) && !Mesh.Indices.empty())
        {
            const std::array<float, 3> Min {Mesh.Bounds.Min.X, Mesh.Bounds.Min.Y, Mesh.Bounds.Min.Z};
            const std::array<float, 3> Max {Mesh.Bounds.Max.X, Mesh.Bounds.Max.Y, Mesh.Bounds.Max.Z};
            const std::array<float, 3> Extent {
                Max[0] - Min[0], Max[1] - Min[1], Max[2] - Min[2]};
            std::array<int, 3> Axes {0, 1, 2};
            std::sort(Axes.begin(), Axes.end(), [&Extent](int Left, int Right)
            {
                return Extent[Left] > Extent[Right];
            });
            const int HorizontalAxis = Axes[0];
            const int VerticalAxis = Axes[1];
            const float HorizontalExtent = std::max(Extent[HorizontalAxis], 0.0001f);
            const float VerticalExtent = std::max(Extent[VerticalAxis], 0.0001f);
            const float Scale = 0.84f / std::max(HorizontalExtent, VerticalExtent);
            const float HorizontalPadding = (1.0f - HorizontalExtent * Scale) * 0.5f;
            const float VerticalPadding = (1.0f - VerticalExtent * Scale) * 0.5f;
            const auto Project = [&](const FVector3& Position)
            {
                const std::array<float, 3> Value {Position.X, Position.Y, Position.Z};
                return std::array<float, 2> {
                    HorizontalPadding + (Value[HorizontalAxis] - Min[HorizontalAxis]) * Scale,
                    1.0f - (VerticalPadding + (Value[VerticalAxis] - Min[VerticalAxis]) * Scale)};
            };
            const std::size_t TriangleCount = Mesh.Indices.size() / 3;
            const std::size_t Step = std::max<std::size_t>(1, TriangleCount / 600);
            for (std::size_t Triangle = 0; Triangle < TriangleCount; Triangle += Step)
            {
                const std::size_t First = Triangle * 3;
                const uint32 I0 = Mesh.Indices[First];
                const uint32 I1 = Mesh.Indices[First + 1];
                const uint32 I2 = Mesh.Indices[First + 2];
                if (I0 >= Mesh.Vertices.size() || I1 >= Mesh.Vertices.size() || I2 >= Mesh.Vertices.size())
                {
                    continue;
                }
                const auto P0 = Project(Mesh.Vertices[I0].Position);
                const auto P1 = Project(Mesh.Vertices[I1].Position);
                const auto P2 = Project(Mesh.Vertices[I2].Position);
                Entry.MeshLines.insert(Entry.MeshLines.end(), {P0, P1, P1, P2, P2, P0});
            }
        }
    }
    return Entry;
}

void FContentBrowserPanel::ReleaseThumbnails()
{
    for (auto& [Path, Thumbnail] : Thumbnails)
    {
        (void)Path;
        if (Thumbnail.Texture != 0)
        {
            glDeleteTextures(1, &Thumbnail.Texture);
            Thumbnail.Texture = 0;
        }
    }
    Thumbnails.clear();
}
}
