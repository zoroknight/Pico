#include "Pico/Core/Paths.h"

#include <system_error>

namespace Pico
{
void FPaths::Init(std::string_view Argv0)
{
    std::error_code ErrorCode;
    std::filesystem::path ArgvPath = std::filesystem::path(std::string(Argv0));

    if (ArgvPath.is_relative())
    {
        ArgvPath = std::filesystem::absolute(ArgvPath, ErrorCode);
    }

    ExecutablePath = std::filesystem::weakly_canonical(ArgvPath, ErrorCode);
    if (ExecutablePath.empty())
    {
        ExecutablePath = ArgvPath;
    }

    ExecutableDir = ExecutablePath.parent_path();

    const std::filesystem::path CurrentRoot = FindProjectRoot(std::filesystem::current_path(ErrorCode));
    if (!CurrentRoot.empty())
    {
        ProjectRootDir = CurrentRoot;
        return;
    }

    const std::filesystem::path ExecutableRoot = FindProjectRoot(ExecutableDir);
    ProjectRootDir = ExecutableRoot.empty() ? std::filesystem::current_path(ErrorCode) : ExecutableRoot;
}

const std::filesystem::path& FPaths::GetExecutablePath()
{
    return ExecutablePath;
}

const std::filesystem::path& FPaths::GetExecutableDir()
{
    return ExecutableDir;
}

const std::filesystem::path& FPaths::GetProjectRootDir()
{
    return ProjectRootDir;
}

std::filesystem::path FPaths::GetConfigDir()
{
    return ProjectRootDir / "Config";
}

std::filesystem::path FPaths::GetProjectConfigFile(std::string_view FileName)
{
    return GetConfigDir() / std::filesystem::path(std::string(FileName));
}

std::filesystem::path FPaths::FindProjectRoot(const std::filesystem::path& StartDir)
{
    std::error_code ErrorCode;
    std::filesystem::path Directory = std::filesystem::weakly_canonical(StartDir, ErrorCode);
    if (Directory.empty())
    {
        Directory = StartDir;
    }

    while (!Directory.empty())
    {
        if (IsProjectRoot(Directory))
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

bool FPaths::IsProjectRoot(const std::filesystem::path& Directory)
{
    std::error_code ErrorCode;
    return std::filesystem::is_regular_file(Directory / "Config" / "Pico.ini", ErrorCode);
}
}
