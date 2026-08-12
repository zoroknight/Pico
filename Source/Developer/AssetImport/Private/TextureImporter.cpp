#include "Pico/AssetImport/TextureImporter.h"

#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <limits>

namespace Pico
{
namespace
{
void Report(ETextureImportError* OutError, ETextureImportError Error)
{
    if (OutError != nullptr) *OutError = Error;
}

bool BuildTexture(stbi_uc* Pixels, int Width, int Height,
    FTextureData& OutTexture, ETextureImportError* OutError)
{
    if (Pixels == nullptr || Width <= 0 || Height <= 0)
    {
        if (Pixels != nullptr) stbi_image_free(Pixels);
        Report(OutError, ETextureImportError::DecodeFailed);
        return false;
    }
    const std::size_t PixelCount = static_cast<std::size_t>(Width) * Height;
    if (PixelCount > std::numeric_limits<std::size_t>::max() / 4)
    {
        stbi_image_free(Pixels);
        Report(OutError, ETextureImportError::InvalidTexture);
        return false;
    }
    FTextureData Texture;
    Texture.Width = static_cast<uint32>(Width);
    Texture.Height = static_cast<uint32>(Height);
    Texture.Pixels.assign(Pixels, Pixels + PixelCount * 4);
    stbi_image_free(Pixels);
    if (!ValidateTexture(Texture))
    {
        Report(OutError, ETextureImportError::InvalidTexture);
        return false;
    }
    OutTexture = std::move(Texture);
    return true;
}
}

bool ImportTexture(
    const std::filesystem::path& SourceFile,
    FTextureData& OutTexture,
    ETextureImportError* OutError)
{
    Report(OutError, ETextureImportError::None);
    if (SourceFile.empty())
    {
        Report(OutError, ETextureImportError::InvalidArgument);
        return false;
    }
    int Width = 0;
    int Height = 0;
    int SourceChannels = 0;
    stbi_uc* Pixels = stbi_load(SourceFile.string().c_str(), &Width, &Height, &SourceChannels, 4);
    return BuildTexture(Pixels, Width, Height, OutTexture, OutError);
}

bool ImportTextureMemory(
    std::span<const uint8> SourceData,
    FTextureData& OutTexture,
    ETextureImportError* OutError)
{
    Report(OutError, ETextureImportError::None);
    if (SourceData.empty() || SourceData.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        Report(OutError, ETextureImportError::InvalidArgument);
        return false;
    }
    int Width = 0;
    int Height = 0;
    int SourceChannels = 0;
    stbi_uc* Pixels = stbi_load_from_memory(
        SourceData.data(), static_cast<int>(SourceData.size()),
        &Width, &Height, &SourceChannels, 4);
    return BuildTexture(Pixels, Width, Height, OutTexture, OutError);
}

bool ImportTextureToFile(
    const std::filesystem::path& SourceFile,
    const std::filesystem::path& DestinationFile,
    ETextureImportError* OutError)
{
    FTextureData Texture;
    if (!ImportTexture(SourceFile, Texture, OutError)) return false;
    if (!SaveTextureToFile(DestinationFile, Texture))
    {
        Report(OutError, ETextureImportError::SaveFailed);
        return false;
    }
    return true;
}

std::string_view ToString(ETextureImportError Error)
{
    switch (Error)
    {
    case ETextureImportError::None: return "None";
    case ETextureImportError::InvalidArgument: return "InvalidArgument";
    case ETextureImportError::DecodeFailed: return "DecodeFailed";
    case ETextureImportError::InvalidTexture: return "InvalidTexture";
    case ETextureImportError::SaveFailed: return "SaveFailed";
    }
    return "Unknown";
}
}
