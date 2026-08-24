#include "Pico/Agent/AgentIntent.h"

#include "Pico/Agent/AgentSkill.h"

#include <algorithm>
#include <array>

namespace Pico
{
namespace
{
template <std::size_t Size>
bool ContainsAny(std::string_view Text,
    const std::array<std::string_view, Size>& Terms)
{
    return std::any_of(Terms.begin(), Terms.end(),
        [Text](std::string_view Term) { return ContainsNonNegatedTerm(Text, Term); });
}
}

EAgentTurnIntent ClassifyAgentTurnIntent(std::string_view Prompt)
{
    constexpr std::array<std::string_view, 12> PackageTerms = {
        "\xE6\x89\x93\xE5\x8C\x85", // package (zh-CN)
        "\xE5\xAF\xBC\xE5\x87\xBA\x45\x58\x45",
        "\xE5\xAF\xBC\xE5\x87\xBA\x20\x45\x58\x45",
        "\xE5\x8F\x91\xE5\xB8\x83\xE7\x89\x88\xE6\x9C\xAC",
        "\xE7\x94\x9F\xE6\x88\x90\xE5\xAE\x89\xE8\xA3\x85\xE5\x8C\x85",
        "package", "packaging", "package project", "package the project",
        "export build", "export executable", "release build"
    };
    if (ContainsAny(Prompt, PackageTerms)) return EAgentTurnIntent::Package;

    constexpr std::array<std::string_view, 12> PlayTerms = {
        "\xE8\xBF\x90\xE8\xA1\x8C\xE9\xA1\xB9\xE7\x9B\xAE",
        "\xE5\x90\xAF\xE5\x8A\xA8\xE9\xA1\xB9\xE7\x9B\xAE",
        "\xE8\xAF\x95\xE7\x8E\xA9",
        "\xE8\xBF\x90\xE8\xA1\x8C\xE6\xB8\xB8\xE6\x88\x8F",
        "\xE5\x90\xAF\xE5\x8A\xA8\xE6\xB8\xB8\xE6\x88\x8F",
        "\xE9\xA2\x84\xE8\xA7\x88\xE6\xB8\xB8\xE6\x88\x8F",
        "play project", "run project", "launch project",
        "preview game", "run game", "play game"
    };
    return ContainsAny(Prompt, PlayTerms)
        ? EAgentTurnIntent::Play : EAgentTurnIntent::General;
}

std::string_view ToString(EAgentTurnIntent Intent)
{
    switch (Intent)
    {
    case EAgentTurnIntent::General: return "General";
    case EAgentTurnIntent::Play: return "Play";
    case EAgentTurnIntent::Package: return "Package";
    }
    return "General";
}
}
