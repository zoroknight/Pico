#pragma once

#include <filesystem>
#include <string_view>

namespace Pico
{
class FPaths
{
public:
    static void Init(std::string_view Argv0);

    static const std::filesystem::path& GetExecutablePath();
    static const std::filesystem::path& GetExecutableDir();
    static const std::filesystem::path& GetProjectRootDir();
    static std::filesystem::path GetConfigDir();
    static std::filesystem::path GetProjectConfigFile(std::string_view FileName);

private:
    static std::filesystem::path FindProjectRoot(const std::filesystem::path& StartDir);
    static bool IsProjectRoot(const std::filesystem::path& Directory);

    static inline std::filesystem::path ExecutablePath;
    static inline std::filesystem::path ExecutableDir;
    static inline std::filesystem::path ProjectRootDir;
};
}
