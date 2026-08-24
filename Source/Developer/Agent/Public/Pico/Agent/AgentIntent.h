#pragma once

#include <string_view>

namespace Pico
{
enum class EAgentTurnIntent
{
    General,
    Play,
    Package
};

EAgentTurnIntent ClassifyAgentTurnIntent(std::string_view Prompt);
std::string_view ToString(EAgentTurnIntent Intent);
}
