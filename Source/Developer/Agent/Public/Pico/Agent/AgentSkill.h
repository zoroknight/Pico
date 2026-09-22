#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
bool ContainsNonNegatedTerm(std::string_view Text, std::string_view Term);

struct FAgentSkill
{
    std::string Id;
    std::string Version;
    std::string Description;
    std::vector<std::string> Triggers;
    std::vector<std::string> RecommendedTools;
    std::vector<std::string> Preconditions;
    std::vector<std::string> Workflow;
    std::vector<std::string> CompletionCriteria;
    std::filesystem::path SourcePath;
};

class FAgentSkillRegistry
{
public:
    bool LoadDirectory(
        const std::filesystem::path& Directory,
        const std::vector<std::string>& AvailableTools,
        std::string* OutError = nullptr);
    std::vector<FAgentSkill> Select(std::string_view Prompt) const;
    std::string BuildSkillContextJson(
        const std::vector<FAgentSkill>& Skills) const;
    std::string PrioritizeToolCatalogJson(
        std::string_view CatalogJson,
        const std::vector<FAgentSkill>& Skills) const;
    const std::vector<FAgentSkill>& GetSkills() const;

private:
    std::vector<FAgentSkill> Skills;
};
}
