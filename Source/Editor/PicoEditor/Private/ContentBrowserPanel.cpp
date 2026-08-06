#include "ContentBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
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
}

void FContentBrowserPanel::Draw(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection,
    FAction Import,
    FAction ImportTexture,
    FAction CreateMaterial,
    FAction Refresh,
    FAssetAction Reimport,
    FAssetAction ReimportWithOptions,
    FAssetsAction DeleteAssets,
    FAssetAction RenameAsset,
    FAssetAction EditMaterial,
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
    constexpr const char* Types[] = {"All Types", "World", "Static Mesh", "Texture", "Material"};
    ImGui::Combo("##AssetType", &TypeFilter, Types, static_cast<int>(std::size(Types)));

    if (ImGui::BeginTable("ContentBrowserLayout", 2, ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        DrawFolderTree(Registry);
        ImGui::TableNextColumn();
        DrawAssetTable(
            Registry,
            Selection,
            Reimport,
            ReimportWithOptions,
            DeleteAssets,
            RenameAsset,
            EditMaterial,
            Create,
            Assign,
            CanReimport);
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
    if (TypeFilter > 0 && static_cast<int>(Record.Type) != TypeFilter - 1)
    {
        return false;
    }
    const std::string Search = ToLower(SearchBuffer.data());
    if (!Search.empty() && ToLower(Record.AssetPath.ToString()).find(Search) == std::string::npos)
    {
        return false;
    }
    return Search.empty() ? GetFolder(Record.AssetPath) == CurrentFolder : true;
}

void FContentBrowserPanel::DrawFolderTree(const FAssetRegistry& Registry)
{
    if (ImGui::Selectable("/Game", CurrentFolder.empty()))
    {
        CurrentFolder.clear();
    }
    std::set<std::string> Folders;
    for (const FAssetRecord& Record : Registry.GetAssets())
    {
        std::filesystem::path Folder(GetFolder(Record.AssetPath));
        std::filesystem::path Accumulated;
        for (const auto& Segment : Folder)
        {
            Accumulated /= Segment;
            Folders.insert(Accumulated.generic_string());
        }
    }
    for (const std::string& Folder : Folders)
    {
        const int Depth = static_cast<int>(std::count(Folder.begin(), Folder.end(), '/')) + 1;
        ImGui::Indent(static_cast<float>(Depth * 12));
        const std::string Label = std::filesystem::path(Folder).filename().string() + "##" + Folder;
        if (ImGui::Selectable(Label.c_str(), CurrentFolder == Folder))
        {
            CurrentFolder = Folder;
        }
        ImGui::Unindent(static_cast<float>(Depth * 12));
    }
}

void FContentBrowserPanel::DrawAssetTable(
    const FAssetRegistry& Registry,
    FEditorAssetSelection& Selection,
    const FAssetAction& Reimport,
    const FAssetAction& ReimportWithOptions,
    const FAssetsAction& DeleteAssets,
    const FAssetAction& RenameAsset,
    const FAssetAction& EditMaterial,
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
        if (ImGui::Selectable(Name.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns))
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
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && Record.Type == EAssetType::StaticMesh)
        {
            Create(Record.AssetPath);
        }
        else if (ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && Record.Type == EAssetType::Material)
        {
            EditMaterial(Record.AssetPath);
        }
        if (ImGui::BeginDragDropSource())
        {
            const std::string Path(Record.AssetPath.ToString());
            ImGui::SetDragDropPayload("PICO_ASSET_PATH", Path.c_str(), Path.size() + 1);
            ImGui::TextUnformatted(Path.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem())
        {
            if (Record.Type != EAssetType::World
                && ImGui::MenuItem("Rename", "F2"))
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
                if (ImGui::MenuItem("Reimport With Options..."))
                {
                    ReimportWithOptions(Record.AssetPath);
                }
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
            ImGui::EndPopup();
        }
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
}
