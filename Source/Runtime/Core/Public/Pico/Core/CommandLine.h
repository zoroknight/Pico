#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class FCommandLine
{
public:
    static void Init(int Argc, char** Argv);

    static bool HasSwitch(std::string_view Name);
    static std::optional<std::string> GetValue(std::string_view Name);
    static std::optional<int> GetInt(std::string_view Name);

    static const std::vector<std::string>& GetArguments();

private:
    static inline std::vector<std::string> Arguments;
};
}
