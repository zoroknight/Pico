#pragma once

#include <filesystem>
#include <optional>

namespace Pico
{
std::optional<std::filesystem::path> OpenObjFileDialog();
std::optional<std::filesystem::path> OpenTextureFileDialog();
}
