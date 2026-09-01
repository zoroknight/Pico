#pragma once

#include "Pico/Core/PlatformProcess.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Pico
{
struct FExternalAgentLaunchContext
{
    std::filesystem::path ProjectRoot;
    std::string EndpointUrl;
    std::string BearerToken;
};

class IExternalAgentConnector
{
public:
    virtual ~IExternalAgentConnector() = default;
    virtual std::string_view GetId() const = 0;
    virtual std::string_view GetDisplayName() const = 0;
    virtual std::string GetClientConfig(std::string_view EndpointUrl) const = 0;
    virtual bool HasClientConfig(const std::filesystem::path& ProjectRoot,
        std::filesystem::path* OutPath = nullptr) const = 0;
    virtual FProcessHandle LaunchCli(const FExternalAgentLaunchContext& Context,
        std::string* OutError = nullptr) const = 0;
    virtual FProcessHandle LaunchDesktop(const FExternalAgentLaunchContext& Context,
        std::string* OutError = nullptr) const = 0;
};

class FCodexExternalAgentConnector final : public IExternalAgentConnector
{
public:
    static constexpr const char* TokenEnvironmentVariable =
        "PICO_MCP_BEARER_TOKEN";

    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    std::string GetClientConfig(std::string_view EndpointUrl) const override;
    bool HasClientConfig(const std::filesystem::path& ProjectRoot,
        std::filesystem::path* OutPath = nullptr) const override;
    FProcessHandle LaunchCli(const FExternalAgentLaunchContext& Context,
        std::string* OutError = nullptr) const override;
    FProcessHandle LaunchDesktop(const FExternalAgentLaunchContext& Context,
        std::string* OutError = nullptr) const override;

private:
    static std::filesystem::path FindExecutable();
};
}
