#include "EditorIconImageLoader.h"

#define STBI_MAX_DIMENSIONS 1024
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <limits>

namespace Pico
{
bool LoadEditorIconImage(
    const std::filesystem::path& FilePath,
    FTextureData& OutTexture)
{
    if (FilePath.empty()) return false;

    int Width = 0;
    int Height = 0;
    int SourceChannels = 0;
    stbi_uc* Pixels = stbi_load(
        FilePath.string().c_str(),
        &Width,
        &Height,
        &SourceChannels,
        4);
    if (Pixels == nullptr || Width <= 0 || Height <= 0)
    {
        if (Pixels != nullptr) stbi_image_free(Pixels);
        return false;
    }

    const std::size_t PixelCount =
        static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height);
    if (PixelCount > std::numeric_limits<std::size_t>::max() / 4)
    {
        stbi_image_free(Pixels);
        return false;
    }

    FTextureData Texture;
    Texture.Width = static_cast<uint32>(Width);
    Texture.Height = static_cast<uint32>(Height);
    Texture.Pixels.assign(Pixels, Pixels + PixelCount * 4);
    stbi_image_free(Pixels);
    if (!ValidateTexture(Texture)) return false;

    OutTexture = std::move(Texture);
    return true;
}
}
