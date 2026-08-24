#include "Pico/Agent/AgentSkill.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::string LowerAscii(std::string Text)
{
    std::transform(Text.begin(), Text.end(), Text.begin(),
        [](unsigned char Character)
        {
            return Character < 128
                ? static_cast<char>(std::tolower(Character))
                : static_cast<char>(Character);
        });
    return Text;
}

bool IsSafeId(std::string_view Id)
{
    return !Id.empty() && Id.size() <= 64
        && std::all_of(Id.begin(), Id.end(), [](unsigned char Character)
        {
            return std::islower(Character) || std::isdigit(Character)
                || Character == '-';
        });
}

bool EndsWith(std::string_view Text, std::string_view Suffix)
{
    return Text.size() >= Suffix.size()
        && Text.substr(Text.size() - Suffix.size()) == Suffix;
}

bool IsNegatedOccurrence(std::string_view Text, std::size_t Position)
{
    std::string_view Prefix = Text.substr(0, Position);
    while (!Prefix.empty() && std::isspace(static_cast<unsigned char>(Prefix.back())))
        Prefix.remove_suffix(1);
    constexpr std::array<std::string_view, 18> Negations = {
        "\xE4\xB8\x8D", "\xE4\xB8\x8D\xE8\xA6\x81", "\xE5\x88\xAB",
        "\xE6\x97\xA0\xE9\x9C\x80", "\xE4\xB8\x8D\xE7\x94\xA8",
        "\xE7\xA6\x81\xE6\xAD\xA2", "\xE5\x88\x87\xE5\x8B\xBF",
        "\xE4\xB8\x8D\xE8\xA6\x81\xE8\xBF\x9B\xE8\xA1\x8C",
        "\xE4\xB8\x8D\xE8\xA6\x81\xE6\x89\xA7\xE8\xA1\x8C",
        "\xE4\xB8\x8D\xE8\xA6\x81\xE8\xB0\x83\xE7\x94\xA8",
        "\xE6\x97\xA0\xE9\x9C\x80\xE8\xBF\x9B\xE8\xA1\x8C",
        "\xE4\xB8\x8D\xE7\x94\xA8\xE8\xBF\x9B\xE8\xA1\x8C",
        "not", "do not", "don't", "without", "never", "no"
    };
    return std::any_of(Negations.begin(), Negations.end(),
        [Prefix](std::string_view Negation) { return EndsWith(Prefix, Negation); });
}
}

bool ContainsNonNegatedTerm(std::string_view Text, std::string_view Term)
{
    if (Term.empty()) return false;
    const std::string LowerText = LowerAscii(std::string(Text));
    const std::string LowerTerm = LowerAscii(std::string(Term));
    std::size_t Position = LowerText.find(LowerTerm);
    while (Position != std::string::npos)
    {
        if (!IsNegatedOccurrence(LowerText, Position)) return true;
        Position = LowerText.find(LowerTerm, Position + LowerTerm.size());
    }
    return false;
}

bool FAgentSkillRegistry::LoadDirectory(
    const std::filesystem::path& Directory,
    const std::vector<std::string>& AvailableTools,
    std::string* OutError)
{
    if (OutError) OutError->clear();
    Skills.clear();
    std::error_code Error;
    if (!std::filesystem::is_directory(Directory, Error)) return !Error;
    const std::set<std::string> ToolSet(AvailableTools.begin(), AvailableTools.end());
    try
    {
        for (const auto& Entry : std::filesystem::directory_iterator(Directory))
        {
            if (!Entry.is_regular_file() || Entry.path().extension() != ".pskill") continue;
            std::ifstream Stream(Entry.path(), std::ios::binary);
            FJson Json;
            Stream >> Json;
            if (Json.value("format_version", 0) != 1)
                throw std::runtime_error("unsupported Skill format: " + Entry.path().string());
            FAgentSkill Skill;
            Skill.Id = Json.value("id", "");
            Skill.Version = Json.value("version", "");
            Skill.Description = Json.value("description", "");
            Skill.Triggers = Json.value("triggers", std::vector<std::string> {});
            Skill.AllowedTools = Json.value("allowed_tools", std::vector<std::string> {});
            Skill.Preconditions = Json.value("preconditions", std::vector<std::string> {});
            Skill.Workflow = Json.value("workflow", std::vector<std::string> {});
            Skill.CompletionCriteria = Json.value(
                "completion_criteria", std::vector<std::string> {});
            Skill.SourcePath = Entry.path();
            if (!IsSafeId(Skill.Id) || Skill.Version.empty()
                || Skill.Description.empty() || Skill.Workflow.empty()
                || Skill.AllowedTools.empty())
                throw std::runtime_error("invalid Skill manifest: " + Entry.path().string());
            for (const std::string& Tool : Skill.AllowedTools)
                if (!ToolSet.contains(Tool))
                    throw std::runtime_error("Skill references unknown tool '" + Tool + "'");
            Skills.push_back(std::move(Skill));
        }
        std::sort(Skills.begin(), Skills.end(),
            [](const auto& Left, const auto& Right) { return Left.Id < Right.Id; });
        return true;
    }
    catch (const std::exception& Exception)
    {
        Skills.clear();
        if (OutError) *OutError = "Could not load Pico Skills: "
            + std::string(Exception.what());
        return false;
    }
}

std::vector<FAgentSkill> FAgentSkillRegistry::Select(std::string_view Prompt) const
{
    std::vector<FAgentSkill> Selected;
    for (const FAgentSkill& Skill : Skills)
        if (std::any_of(Skill.Triggers.begin(), Skill.Triggers.end(),
            [Prompt](const std::string& Trigger)
            {
                return !Trigger.empty()
                    && ContainsNonNegatedTerm(Prompt, Trigger);
            }))
            Selected.push_back(Skill);
    return Selected;
}

std::string FAgentSkillRegistry::BuildSkillContextJson(
    const std::vector<FAgentSkill>& Selected) const
{
    FJson Result = FJson::array();
    for (const FAgentSkill& Skill : Selected)
        Result.push_back({{"id", Skill.Id}, {"version", Skill.Version},
            {"description", Skill.Description}, {"allowed_tools", Skill.AllowedTools},
            {"preconditions", Skill.Preconditions}, {"workflow", Skill.Workflow},
            {"completion_criteria", Skill.CompletionCriteria},
            {"security", "This Skill narrows available tools and never bypasses validation, approval, transactions, or verification."}});
    return Result.dump();
}

std::string FAgentSkillRegistry::FilterToolCatalogJson(
    std::string_view CatalogJson,
    const std::vector<FAgentSkill>& Selected) const
{
    if (Selected.empty()) return std::string(CatalogJson);
    std::set<std::string> Allowed;
    for (const FAgentSkill& Skill : Selected)
        Allowed.insert(Skill.AllowedTools.begin(), Skill.AllowedTools.end());
    try
    {
        const FJson Catalog = FJson::parse(CatalogJson);
        FJson Filtered = FJson::array();
        for (const FJson& Tool : Catalog)
            if (Allowed.contains(Tool.value("name", ""))) Filtered.push_back(Tool);
        return Filtered.dump();
    }
    catch (...) { return "[]"; }
}

const std::vector<FAgentSkill>& FAgentSkillRegistry::GetSkills() const { return Skills; }
}
