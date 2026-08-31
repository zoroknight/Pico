#pragma once

#include "Pico/Asset/Texture.h"

#include <filesystem>

namespace Pico
{
bool LoadEditorIconImage(
    const std::filesystem::path& FilePath,
    FTextureData& OutTexture);
}
