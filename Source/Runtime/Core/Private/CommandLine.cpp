#include "Pico/Core/CommandLine.h"

#include <charconv>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <system_error>

namespace Pico
{
void FCommandLine::Init(int Argc, char** Argv)
{
    Arguments.clear();
    Arguments.reserve(static_cast<std::size_t>(Argc));

    for (int Index = 0; Index < Argc; ++Index)
    {
        Arguments.emplace_back(Argv[Index]);
    }
}

bool FCommandLine::HasSwitch(std::string_view Name)
{
    const std::string Switch = std::string("-") + std::string(Name);

    for (const std::string& Argument : Arguments)
    {
        if (Argument == Switch)
        {
            return true;
        }
    }

    return false;
}

std::optional<std::string> FCommandLine::GetValue(std::string_view Name)
{
    const std::string Prefix = std::string("-") + std::string(Name) + "=";

    for (const std::string& Argument : Arguments)
    {
        if (Argument.starts_with(Prefix))
        {
            return Argument.substr(Prefix.size());
        }
    }

    return std::nullopt;
}

std::optional<int> FCommandLine::GetInt(std::string_view Name)
{
    const std::optional<std::string> Value = GetValue(Name);
    if (!Value.has_value())
    {
        return std::nullopt;
    }

    int Result = 0;
    const char* Begin = Value->data();
    const char* End = Begin + Value->size();
    const std::from_chars_result ParseResult = std::from_chars(Begin, End, Result);

    if (ParseResult.ec != std::errc{} || ParseResult.ptr != End)
    {
        return std::nullopt;
    }

    return Result;
}

std::optional<bool> FCommandLine::GetBool(std::string_view Name)
{
    const std::optional<std::string> Value = GetValue(Name);
    if (!Value.has_value())
    {
        return std::nullopt;
    }

    std::string Lower = *Value;
    std::transform(
        Lower.begin(), Lower.end(), Lower.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    if (Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on")
    {
        return true;
    }
    if (Lower == "false" || Lower == "0" || Lower == "no" || Lower == "off")
    {
        return false;
    }
    return std::nullopt;
}

const std::vector<std::string>& FCommandLine::GetArguments()
{
    return Arguments;
}
}
