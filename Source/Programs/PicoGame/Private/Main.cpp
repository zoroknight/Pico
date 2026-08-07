#include "Pico/Launch/GameLaunch.h"

#include <filesystem>

#ifndef PICO_DEFAULT_PROJECT_FILE
#define PICO_DEFAULT_PROJECT_FILE ""
#endif

int main(int Argc, char** Argv)
{
    return Pico::RunPicoGame(
        Argc,
        Argv,
        nullptr,
        std::filesystem::path(PICO_DEFAULT_PROJECT_FILE));
}
