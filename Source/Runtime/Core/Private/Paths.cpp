#include "Pico/Core/Paths.h"

#include "Pico/Core/Config.h"

#include <array>
#include <string>
#include <system_error>

namespace Pico
{
bool FPaths::Init(
    std::string_view Argv0,
    const std::filesystem::path& ProjectFile,
    const FPathInitOptions& Options)
{
    ExecutablePath.clear();
    ExecutableDir.clear();
    EngineRootDir.clear();
    StageRootDir.clear();
    ProjectFilePath.clear();
    ProjectRootDir.clear();
    EngineLayoutMode = EEngineLayoutMode::Unknown;
    LayoutEngineVersion.clear();
    StageProjectName.clear();

    std::error_code ErrorCode;
    std::filesystem::path ArgvPath = std::filesystem::path(std::string(Argv0));
    if (!ArgvPath.empty() && ArgvPath.is_relative())
    {
        ArgvPath = std::filesystem::absolute(ArgvPath, ErrorCode);
    }

    if (!ArgvPath.empty())
    {
        ExecutablePath = NormalizePath(ArgvPath);
        ExecutableDir = ExecutablePath.parent_path();
    }

    const std::filesystem::path CurrentDirectory =
        std::filesystem::current_path(ErrorCode);

    if (!Options.ExplicitStageRoot.empty())
    {
        return InitializeStage(
            NormalizePath(Options.ExplicitStageRoot), ProjectFile);
    }

    if (!Options.ExplicitEngineRoot.empty())
    {
        EngineRootDir = NormalizePath(Options.ExplicitEngineRoot);
        EngineLayoutMode = DetectEngineRoot(EngineRootDir);
        if (EngineLayoutMode == EEngineLayoutMode::Unknown)
        {
            EngineRootDir.clear();
            return false;
        }
        if (EngineLayoutMode == EEngineLayoutMode::Installed)
        {
            FConfigFile Marker;
            Marker.Load(EngineRootDir / "PicoEngine.root");
            LayoutEngineVersion = Marker.GetString("Engine", "Version", "");
        }
    }
    else
    {
        const std::filesystem::path AutomaticStageRoot =
            FindStageRoot(ExecutableDir);
        if (!AutomaticStageRoot.empty())
        {
            return InitializeStage(AutomaticStageRoot, ProjectFile);
        }

        EngineRootDir = FindEngineRoot(CurrentDirectory);
        if (EngineRootDir.empty())
        {
            EngineRootDir = FindEngineRoot(ExecutableDir);
        }
        EngineLayoutMode = DetectEngineRoot(EngineRootDir);
        if (EngineLayoutMode == EEngineLayoutMode::Unknown)
        {
            EngineRootDir.clear();
            return false;
        }
        if (EngineLayoutMode == EEngineLayoutMode::Installed)
        {
            FConfigFile Marker;
            Marker.Load(EngineRootDir / "PicoEngine.root");
            LayoutEngineVersion = Marker.GetString("Engine", "Version", "");
        }
    }

    if (ProjectFile.empty())
    {
        return true;
    }
    std::filesystem::path Candidate = ProjectFile;
    if (Candidate.is_relative())
    {
        Candidate = CurrentDirectory / Candidate;
    }
    Candidate = NormalizePath(Candidate);

    if (Candidate.extension() != ".pico"
        || !std::filesystem::is_regular_file(Candidate, ErrorCode))
    {
        return false;
    }

    ProjectFilePath = Candidate;
    ProjectRootDir = NormalizePath(Candidate.parent_path());
    return !ProjectRootDir.empty();
}

const std::filesystem::path& FPaths::GetExecutablePath()
{
    return ExecutablePath;
}

const std::filesystem::path& FPaths::GetExecutableDir()
{
    return ExecutableDir;
}

const std::filesystem::path& FPaths::GetEngineRootDir()
{
    return EngineRootDir;
}

EEngineLayoutMode FPaths::GetEngineLayoutMode()
{
    return EngineLayoutMode;
}

const std::string& FPaths::GetLayoutEngineVersion()
{
    return LayoutEngineVersion;
}

bool FPaths::IsStaged()
{
    return EngineLayoutMode == EEngineLayoutMode::Staged;
}

const std::filesystem::path& FPaths::GetStageRootDir()
{
    return StageRootDir;
}

const std::string& FPaths::GetStageProjectName()
{
    return StageProjectName;
}

std::filesystem::path FPaths::GetEngineConfigDir()
{
    return EngineRootDir.empty() ? std::filesystem::path {} : EngineRootDir / "Config";
}

std::filesystem::path FPaths::GetEngineConfigFile(std::string_view FileName)
{
    const std::filesystem::path ConfigDir = GetEngineConfigDir();
    return ConfigDir.empty()
        ? std::filesystem::path {}
        : ConfigDir / std::filesystem::path(std::string(FileName));
}

bool FPaths::HasProject()
{
    return !ProjectFilePath.empty() && !ProjectRootDir.empty();
}

const std::filesystem::path& FPaths::GetProjectFile()
{
    return ProjectFilePath;
}

const std::filesystem::path& FPaths::GetProjectRootDir()
{
    return ProjectRootDir;
}

std::filesystem::path FPaths::GetProjectConfigDir()
{
    return HasProject() ? ProjectRootDir / "Config" : std::filesystem::path {};
}

std::filesystem::path FPaths::GetProjectConfigFile(std::string_view FileName)
{
    const std::filesystem::path ConfigDir = GetProjectConfigDir();
    return ConfigDir.empty()
        ? std::filesystem::path {}
        : ConfigDir / std::filesystem::path(std::string(FileName));
}

std::filesystem::path FPaths::GetProjectContentDir()
{
    return HasProject() ? ProjectRootDir / "Content" : std::filesystem::path {};
}

std::filesystem::path FPaths::GetProjectIntermediateDir()
{
    return HasProject() ? ProjectRootDir / "Intermediate" : std::filesystem::path {};
}

std::filesystem::path FPaths::GetProjectSavedDir()
{
    return HasProject() ? ProjectRootDir / "Saved" : std::filesystem::path {};
}

bool FPaths::TryGetProjectWritePath(
    EProjectWriteRoot Root,
    const std::filesystem::path& RelativePath,
    std::filesystem::path& OutPath)
{
    OutPath.clear();
    if (!HasProject() || RelativePath.empty() || RelativePath.is_absolute())
    {
        return false;
    }

    std::filesystem::path RootPath;
    switch (Root)
    {
    case EProjectWriteRoot::Content:
        RootPath = GetProjectContentDir();
        break;
    case EProjectWriteRoot::Intermediate:
        RootPath = GetProjectIntermediateDir();
        break;
    case EProjectWriteRoot::Saved:
        RootPath = GetProjectSavedDir();
        break;
    }

    const std::filesystem::path Candidate = NormalizePath(RootPath / RelativePath);
    if (!IsWithin(Candidate, RootPath))
    {
        return false;
    }

    OutPath = Candidate;
    return true;
}

bool FPaths::IsProjectWritePath(const std::filesystem::path& Path)
{
    if (!HasProject() || Path.empty())
    {
        return false;
    }

    const std::filesystem::path Candidate = NormalizePath(Path);
    const std::array<std::filesystem::path, 3> WriteRoots {
        GetProjectContentDir(),
        GetProjectIntermediateDir(),
        GetProjectSavedDir()
    };

    for (const std::filesystem::path& Root : WriteRoots)
    {
        if (IsWithin(Candidate, Root))
        {
            return true;
        }
    }
    return false;
}

std::filesystem::path FPaths::FindEngineRoot(const std::filesystem::path& StartDir)
{
    std::filesystem::path Directory = NormalizePath(StartDir);
    while (!Directory.empty())
    {
        if (DetectEngineRoot(Directory) != EEngineLayoutMode::Unknown)
        {
            return Directory;
        }

        const std::filesystem::path Parent = Directory.parent_path();
        if (Parent == Directory)
        {
            break;
        }
        Directory = Parent;
    }
    return {};
}

std::filesystem::path FPaths::FindStageRoot(const std::filesystem::path& StartDir)
{
    std::filesystem::path Directory = NormalizePath(StartDir);
    while (!Directory.empty())
    {
        std::error_code ErrorCode;
        if (std::filesystem::is_regular_file(
                Directory / "PicoStage.manifest", ErrorCode))
        {
            return Directory;
        }
        const std::filesystem::path Parent = Directory.parent_path();
        if (Parent == Directory)
        {
            break;
        }
        Directory = Parent;
    }
    return {};
}

EEngineLayoutMode FPaths::DetectEngineRoot(
    const std::filesystem::path& Directory)
{
    if (Directory.empty())
    {
        return EEngineLayoutMode::Unknown;
    }
    if (IsDevelopmentEngineRoot(Directory))
    {
        return EEngineLayoutMode::Development;
    }
    if (IsInstalledEngineRoot(Directory))
    {
        return EEngineLayoutMode::Installed;
    }
    return EEngineLayoutMode::Unknown;
}

bool FPaths::IsDevelopmentEngineRoot(const std::filesystem::path& Directory)
{
    std::error_code ErrorCode;
    return std::filesystem::is_regular_file(Directory / "CMakeLists.txt", ErrorCode)
        && std::filesystem::is_directory(
            Directory / "Source" / "Runtime" / "Core",
            ErrorCode)
        && std::filesystem::is_regular_file(
            Directory / "Config" / "Pico.ini",
            ErrorCode);
}

bool FPaths::IsInstalledEngineRoot(const std::filesystem::path& Directory)
{
    std::error_code ErrorCode;
    const std::filesystem::path Marker = Directory / "PicoEngine.root";
    if (!std::filesystem::is_regular_file(Marker, ErrorCode)
        || !std::filesystem::is_regular_file(
            Directory / "Config" / "Pico.ini", ErrorCode))
    {
        return false;
    }

    FConfigFile Config;
    return Config.Load(Marker)
        && Config.GetString("Engine", "Name", "") == "Pico"
        && !Config.GetString("Engine", "Version", "").empty()
        && Config.GetInt("Engine", "LayoutVersion", 0) == 1;
}

bool FPaths::InitializeStage(
    const std::filesystem::path& StageRoot,
    const std::filesystem::path& RequestedProjectFile)
{
    StageRootDir = NormalizePath(StageRoot);
    FConfigFile Manifest;
    if (StageRootDir.empty()
        || !Manifest.Load(StageRootDir / "PicoStage.manifest")
        || Manifest.GetInt("Stage", "LayoutVersion", 0) != 1)
    {
        StageRootDir.clear();
        return false;
    }

    const std::filesystem::path EngineRelativePath(
        Manifest.GetString("Stage", "EngineRelativePath", ""));
    const std::filesystem::path ProjectRelativePath(
        Manifest.GetString("Stage", "ProjectRelativePath", ""));
    const std::string ManifestEngineVersion =
        Manifest.GetString("Stage", "EngineVersion", "");
    const std::string ManifestProjectName =
        Manifest.GetString("Stage", "ProjectName", "");
    if (EngineRelativePath.empty()
        || EngineRelativePath.is_absolute()
        || ProjectRelativePath.empty()
        || ProjectRelativePath.is_absolute()
        || ManifestEngineVersion.empty()
        || ManifestProjectName.empty())
    {
        StageRootDir.clear();
        return false;
    }

    EngineRootDir = NormalizePath(StageRootDir / EngineRelativePath);
    const std::filesystem::path ManifestProjectFile =
        NormalizePath(StageRootDir / ProjectRelativePath);
    if (!IsWithin(EngineRootDir, StageRootDir)
        || !IsWithin(ManifestProjectFile, StageRootDir)
        || !IsInstalledEngineRoot(EngineRootDir))
    {
        EngineRootDir.clear();
        StageRootDir.clear();
        return false;
    }

    FConfigFile EngineMarker;
    EngineMarker.Load(EngineRootDir / "PicoEngine.root");
    const std::string InstalledEngineVersion =
        EngineMarker.GetString("Engine", "Version", "");
    if (InstalledEngineVersion != ManifestEngineVersion)
    {
        EngineRootDir.clear();
        StageRootDir.clear();
        return false;
    }

    std::filesystem::path Candidate = ManifestProjectFile;
    if (!RequestedProjectFile.empty())
    {
        Candidate = RequestedProjectFile;
        if (Candidate.is_relative())
        {
            Candidate = StageRootDir / Candidate;
        }
        Candidate = NormalizePath(Candidate);
    }

    std::error_code ErrorCode;
    if (!IsWithin(Candidate, StageRootDir)
        || Candidate.extension() != ".pico"
        || !std::filesystem::is_regular_file(Candidate, ErrorCode))
    {
        EngineRootDir.clear();
        StageRootDir.clear();
        return false;
    }

    EngineLayoutMode = EEngineLayoutMode::Staged;
    LayoutEngineVersion = ManifestEngineVersion;
    StageProjectName = ManifestProjectName;
    ProjectFilePath = Candidate;
    ProjectRootDir = NormalizePath(Candidate.parent_path());
    return !ProjectRootDir.empty();
}

std::filesystem::path FPaths::NormalizePath(const std::filesystem::path& Path)
{
    if (Path.empty())
    {
        return {};
    }

    std::error_code ErrorCode;
    std::filesystem::path AbsolutePath = Path;
    if (AbsolutePath.is_relative())
    {
        AbsolutePath = std::filesystem::absolute(AbsolutePath, ErrorCode);
    }

    const std::filesystem::path CanonicalPath =
        std::filesystem::weakly_canonical(AbsolutePath, ErrorCode);
    return CanonicalPath.empty() ? AbsolutePath.lexically_normal() : CanonicalPath;
}

bool FPaths::IsWithin(
    const std::filesystem::path& Candidate,
    const std::filesystem::path& Root)
{
    if (Candidate.empty() || Root.empty())
    {
        return false;
    }

    const std::filesystem::path NormalCandidate = NormalizePath(Candidate);
    const std::filesystem::path NormalRoot = NormalizePath(Root);
    const std::filesystem::path Relative = NormalCandidate.lexically_relative(NormalRoot);
    if (Relative.empty() || Relative == ".")
    {
        return false;
    }

    const auto FirstPart = Relative.begin();
    return FirstPart != Relative.end() && *FirstPart != "..";
}
}
