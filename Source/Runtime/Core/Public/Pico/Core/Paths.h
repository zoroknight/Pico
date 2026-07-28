#pragma once

#include <filesystem>
#include <string_view>

namespace Pico
{
enum class EProjectWriteRoot
{
    Content,
    Intermediate,
    Saved
};

class FPaths
{
public:
    static bool Init(
        std::string_view Argv0,
        const std::filesystem::path& ProjectFile = {});

    static const std::filesystem::path& GetExecutablePath();
    static const std::filesystem::path& GetExecutableDir();

    static const std::filesystem::path& GetEngineRootDir();
    static std::filesystem::path GetEngineConfigDir();
    static std::filesystem::path GetEngineConfigFile(std::string_view FileName);

    static bool HasProject();
    static const std::filesystem::path& GetProjectFile();
    static const std::filesystem::path& GetProjectRootDir();
    static std::filesystem::path GetProjectConfigDir();
    static std::filesystem::path GetProjectConfigFile(std::string_view FileName);
    static std::filesystem::path GetProjectContentDir();
    static std::filesystem::path GetProjectIntermediateDir();
    static std::filesystem::path GetProjectSavedDir();

    static bool TryGetProjectWritePath(
        EProjectWriteRoot Root,
        const std::filesystem::path& RelativePath,
        std::filesystem::path& OutPath);
    static bool IsProjectWritePath(const std::filesystem::path& Path);

private:
    static std::filesystem::path FindEngineRoot(const std::filesystem::path& StartDir);
    static bool IsEngineRoot(const std::filesystem::path& Directory);
    static std::filesystem::path NormalizePath(const std::filesystem::path& Path);
    static bool IsWithin(
        const std::filesystem::path& Candidate,
        const std::filesystem::path& Root);

    static inline std::filesystem::path ExecutablePath;
    static inline std::filesystem::path ExecutableDir;
    static inline std::filesystem::path EngineRootDir;
    static inline std::filesystem::path ProjectFilePath;
    static inline std::filesystem::path ProjectRootDir;
};
}
