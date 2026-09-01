#include "Pico/Editor/EditorProjectManager.h"

#include "Pico/Core/Config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <system_error>

namespace Pico
{
namespace
{
std::string ToLower(std::string Value)
{
    std::transform(
        Value.begin(), Value.end(), Value.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    return Value;
}

bool IsProjectDescriptor(const std::filesystem::path& FilePath)
{
    return std::filesystem::is_regular_file(FilePath)
        && ToLower(FilePath.extension().string()) == ".pico";
}

std::filesystem::path NormalizeExistingPath(
    const std::filesystem::path& FilePath)
{
    std::error_code Error;
    std::filesystem::path Result = std::filesystem::weakly_canonical(
        std::filesystem::absolute(FilePath, Error), Error);
    return Error ? FilePath.lexically_normal() : Result;
}

bool PathsEqual(
    const std::filesystem::path& Left,
    const std::filesystem::path& Right)
{
    const std::string LeftText = Left.generic_string();
    const std::string RightText = Right.generic_string();
#if defined(_WIN32)
    return ToLower(LeftText) == ToLower(RightText);
#else
    return LeftText == RightText;
#endif
}

std::optional<std::filesystem::path> GetEnvironmentPath(const char* Name)
{
#if defined(_WIN32)
    char* Value = nullptr;
    std::size_t Length = 0;
    if (_dupenv_s(&Value, &Length, Name) != 0 || Value == nullptr)
    {
        return std::nullopt;
    }
    const std::filesystem::path Result(Value);
    std::free(Value);
    return Result;
#else
    const char* Value = std::getenv(Name);
    return Value != nullptr
        ? std::optional<std::filesystem::path>(std::filesystem::path(Value))
        : std::nullopt;
#endif
}

void ParseAssetPath(
    const FConfigFile& Config,
    std::string_view Key,
    FAssetPath& OutPath)
{
    FAssetPath Parsed;
    if (FAssetPath::TryParse(
            Config.GetString("Session", Key, ""), Parsed))
    {
        OutPath = Parsed;
    }
}
}

FEditorProjectResolution ResolveEditorProjectPath(
    const std::filesystem::path& Selection)
{
    FEditorProjectResolution Result;
    if (Selection.empty())
    {
        Result.Message = "Select a Pico project file or project folder";
        return Result;
    }

    std::error_code Error;
    if (std::filesystem::is_regular_file(Selection, Error))
    {
        if (!IsProjectDescriptor(Selection))
        {
            Result.Message = "The selected file is not a .pico project descriptor";
            return Result;
        }
        Result.ProjectFile = NormalizeExistingPath(Selection);
        return Result;
    }
    if (!std::filesystem::is_directory(Selection, Error))
    {
        Result.Message = "The selected project path does not exist";
        return Result;
    }

    for (const std::filesystem::directory_entry& Entry :
         std::filesystem::directory_iterator(Selection, Error))
    {
        if (Error)
        {
            Result.Message = "Could not inspect the selected project folder";
            return Result;
        }
        if (IsProjectDescriptor(Entry.path()))
        {
            Result.Candidates.push_back(NormalizeExistingPath(Entry.path()));
        }
    }
    std::sort(Result.Candidates.begin(), Result.Candidates.end());
    if (Result.Candidates.size() == 1)
    {
        Result.ProjectFile = Result.Candidates.front();
        return Result;
    }
    Result.Message = Result.Candidates.empty()
        ? "The selected folder does not contain a .pico project"
        : "The selected folder contains multiple .pico projects; select one file";
    return Result;
}

std::filesystem::path GetEditorUserSettingsFile()
{
#if defined(_WIN32)
    if (const auto LocalAppData = GetEnvironmentPath("LOCALAPPDATA"))
    {
        return *LocalAppData / "PicoEditor" / "EditorSettings.ini";
    }
#endif
    if (const auto Home = GetEnvironmentPath("HOME"))
    {
        return *Home / ".config" / "PicoEditor" / "EditorSettings.ini";
    }
    return std::filesystem::current_path()
        / "Saved" / "Editor" / "EditorSettings.ini";
}

bool FEditorProjectHistory::Load(const std::filesystem::path& FilePath)
{
    RecentProjects.clear();
    FConfigFile Config;
    if (!Config.Load(FilePath))
    {
        return false;
    }
    for (std::size_t Index = MaxRecentProjects; Index-- > 0;)
    {
        const std::string Value = Config.GetString(
            "RecentProjects", "Project" + std::to_string(Index), "");
        const FEditorProjectResolution Resolution =
            ResolveEditorProjectPath(std::filesystem::path(Value));
        if (Resolution.IsResolved())
        {
            Add(Resolution.ProjectFile);
        }
    }
    return true;
}

bool FEditorProjectHistory::Save(const std::filesystem::path& FilePath) const
{
    FConfigFile Config;
    for (std::size_t Index = 0; Index < RecentProjects.size(); ++Index)
    {
        Config.SetString(
            "RecentProjects",
            "Project" + std::to_string(Index),
            RecentProjects[Index].string());
    }
    return Config.Save(FilePath);
}

void FEditorProjectHistory::Add(const std::filesystem::path& ProjectFile)
{
    const FEditorProjectResolution Resolution = ResolveEditorProjectPath(ProjectFile);
    if (!Resolution.IsResolved())
    {
        return;
    }
    Remove(Resolution.ProjectFile);
    RecentProjects.insert(RecentProjects.begin(), Resolution.ProjectFile);
    if (RecentProjects.size() > MaxRecentProjects)
    {
        RecentProjects.resize(MaxRecentProjects);
    }
}

void FEditorProjectHistory::Remove(const std::filesystem::path& ProjectFile)
{
    std::erase_if(
        RecentProjects,
        [&ProjectFile](const std::filesystem::path& Recent)
        {
            return PathsEqual(Recent, ProjectFile);
        });
}

bool FEditorSessionState::Load(const std::filesystem::path& FilePath)
{
    LastWorld = {};
    OpenActorBlueprint = {};
    OpenSkeletalAsset = {};
    bAgentChatOpen = true;
    bExternalAgentsOpen = false;
    FConfigFile Config;
    if (!Config.Load(FilePath))
    {
        return false;
    }
    ParseAssetPath(Config, "LastWorld", LastWorld);
    ParseAssetPath(Config, "OpenActorBlueprint", OpenActorBlueprint);
    ParseAssetPath(Config, "OpenSkeletalAsset", OpenSkeletalAsset);
    bAgentChatOpen = Config.GetBool("Workspace", "AgentChatOpen", true);
    bExternalAgentsOpen =
        Config.GetBool("Workspace", "ExternalAgentsOpen", false);
    return true;
}

bool FEditorSessionState::Save(const std::filesystem::path& FilePath) const
{
    FConfigFile Config;
    if (LastWorld.IsValid())
    {
        Config.SetString(
            "Session", "LastWorld", std::string(LastWorld.ToString()));
    }
    if (OpenActorBlueprint.IsValid())
    {
        Config.SetString(
            "Session", "OpenActorBlueprint",
            std::string(OpenActorBlueprint.ToString()));
    }
    if (OpenSkeletalAsset.IsValid())
    {
        Config.SetString(
            "Session", "OpenSkeletalAsset",
            std::string(OpenSkeletalAsset.ToString()));
    }
    Config.SetString(
        "Workspace", "AgentChatOpen", bAgentChatOpen ? "true" : "false");
    Config.SetString(
        "Workspace", "ExternalAgentsOpen",
        bExternalAgentsOpen ? "true" : "false");
    return Config.Save(FilePath);
}
}
