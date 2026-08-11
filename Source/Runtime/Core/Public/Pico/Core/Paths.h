#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Pico
{
enum class EEngineLayoutMode
{
    Unknown,
    Development,
    Installed,
    Staged
};

struct FPathInitOptions
{
    std::filesystem::path ExplicitEngineRoot;
    std::filesystem::path ExplicitStageRoot;
};

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
        const std::filesystem::path& ProjectFile = {},
        const FPathInitOptions& Options = {});

    static const std::filesystem::path& GetExecutablePath();
    static const std::filesystem::path& GetExecutableDir();

    static const std::filesystem::path& GetEngineRootDir();
    static EEngineLayoutMode GetEngineLayoutMode();
    static const std::string& GetLayoutEngineVersion();
    static bool IsStaged();
    static const std::filesystem::path& GetStageRootDir();
    static const std::string& GetStageProjectName();
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
    static std::filesystem::path FindStageRoot(const std::filesystem::path& StartDir);
    static EEngineLayoutMode DetectEngineRoot(const std::filesystem::path& Directory);
    static bool IsDevelopmentEngineRoot(const std::filesystem::path& Directory);
    static bool IsInstalledEngineRoot(const std::filesystem::path& Directory);
    static bool InitializeStage(
        const std::filesystem::path& StageRoot,
        const std::filesystem::path& RequestedProjectFile);
    static std::filesystem::path NormalizePath(const std::filesystem::path& Path);
    static bool IsWithin(
        const std::filesystem::path& Candidate,
        const std::filesystem::path& Root);

    static inline std::filesystem::path ExecutablePath;
    static inline std::filesystem::path ExecutableDir;
    static inline std::filesystem::path EngineRootDir;
    static inline std::filesystem::path StageRootDir;
    static inline std::filesystem::path ProjectFilePath;
    static inline std::filesystem::path ProjectRootDir;
    static inline EEngineLayoutMode EngineLayoutMode = EEngineLayoutMode::Unknown;
    static inline std::string LayoutEngineVersion;
    static inline std::string StageProjectName;
};
}
