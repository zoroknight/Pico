#pragma once

#include <filesystem>

namespace Pico
{
class IGameModule;

int RunPicoGame(
    int Argc,
    char** Argv,
    IGameModule* GameModule = nullptr,
    const std::filesystem::path& DefaultProjectFile = {});
}
