#pragma once

#include "Pico/Core/PlatformProcess.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Pico
{
enum class EEditorPlayNetMode
{
    Standalone,
    SeparateServer,
    ListenServer
};

enum class EPlayProcessRole
{
    Standalone,
    Server,
    Client
};

struct FPlaySessionSettings
{
    EEditorPlayNetMode NetMode = EEditorPlayNetMode::Standalone;
    int PlayerCount = 1;
    int ServerPort = 17777;
    int ClientWindowWidth = 960;
    int ClientWindowHeight = 540;

    void Clamp();
    bool Validate(std::string& OutError) const;
    bool Load(const std::filesystem::path& FilePath);
    bool Save(const std::filesystem::path& FilePath) const;
};

struct FPlayProcessSpec
{
    EPlayProcessRole Role = EPlayProcessRole::Standalone;
    std::string Label;
    std::vector<std::string> Arguments;
    std::filesystem::path LogFile;
};

struct FPlaySessionLaunchRequest
{
    FPlaySessionSettings Settings;
    std::filesystem::path Executable;
    std::filesystem::path ProjectFile;
    std::string MapAssetPath;
    std::filesystem::path WorkingDirectory;
    std::filesystem::path LogDirectory;
};

struct FPlayProcessExit
{
    std::string Label;
    int ExitCode = 0;
    std::filesystem::path LogFile;
};

const char* ToString(EEditorPlayNetMode Mode);

bool BuildPlayProcessSpecs(
    const FPlaySessionLaunchRequest& Request,
    std::vector<FPlayProcessSpec>& OutSpecs,
    std::string& OutError);

class FPlaySession
{
public:
    ~FPlaySession();

    FPlaySession(const FPlaySession&) = delete;
    FPlaySession& operator=(const FPlaySession&) = delete;
    FPlaySession() = default;

    bool Start(const FPlaySessionLaunchRequest& Request, std::string& OutError);
    bool Stop();
    std::vector<FPlayProcessExit> Poll();

    bool IsActive() const { return !Processes.empty(); }
    std::size_t GetProcessCount() const { return Processes.size(); }
    const std::filesystem::path& GetLogDirectory() const { return LogDirectory; }

private:
    struct FManagedProcess
    {
        std::string Label;
        std::filesystem::path LogFile;
        FProcessHandle Handle;
    };

    std::vector<FManagedProcess> Processes;
    std::filesystem::path LogDirectory;
};
}
