#pragma once

#include <filesystem>

namespace Pico::Detail
{
bool ReplaceSerializedFile(
    const std::filesystem::path& TemporaryPath,
    const std::filesystem::path& FilePath);
}
