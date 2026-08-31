#pragma once

#include <filesystem>
#include <string>

namespace Pico
{
bool IsDevelopmentGameRuntimeStale(
    const std::filesystem::path& GameExecutable,
    std::string& OutDependency);
}
