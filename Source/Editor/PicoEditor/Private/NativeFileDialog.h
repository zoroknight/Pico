#pragma once

#include <filesystem>
#include <optional>

namespace Pico
{
std::optional<std::filesystem::path> OpenObjFileDialog();
std::optional<std::filesystem::path> OpenTextureFileDialog();
std::optional<std::filesystem::path> OpenWorldFileDialog(
    const std::filesystem::path& InitialDirectory);
std::optional<std::filesystem::path> SaveWorldFileDialog(
    const std::filesystem::path& InitialDirectory,
    const std::filesystem::path& SuggestedFile = {});
}
