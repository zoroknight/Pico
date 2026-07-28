#include "Pico/Core/Paths.h"

#include <array>
#include <system_error>

namespace Pico
{
bool FPaths::Init(std::string_view Argv0, const std::filesystem::path& ProjectFile)
{
    ExecutablePath.clear();
    ExecutableDir.clear();
    EngineRootDir.clear();
    ProjectFilePath.clear();
    ProjectRootDir.clear();

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
    EngineRootDir = FindEngineRoot(CurrentDirectory);
    if (EngineRootDir.empty())
    {
        EngineRootDir = FindEngineRoot(ExecutableDir);
    }
    if (EngineRootDir.empty())
    {
        return false;
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
        if (IsEngineRoot(Directory))
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

bool FPaths::IsEngineRoot(const std::filesystem::path& Directory)
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
