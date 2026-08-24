#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Pico
{
struct FAgentProjectHandoff
{
    std::filesystem::path SourceProjectFile;
    std::filesystem::path TargetProjectFile;
    std::filesystem::path SessionPath;
    std::string SessionId;
    std::string Provider;
    std::string Model;
    std::string Goal;
};

bool PrepareAgentProjectHandoff(
    const FAgentProjectHandoff& Handoff,
    std::string* OutError = nullptr);

std::optional<FAgentProjectHandoff> ConsumeAgentProjectHandoff(
    const std::filesystem::path& ProjectRoot,
    std::string* OutError = nullptr);
}
