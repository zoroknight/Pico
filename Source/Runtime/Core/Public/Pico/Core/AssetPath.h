#pragma once

#include <string>
#include <string_view>

namespace Pico
{
enum class EAssetPathError
{
    None,
    Empty,
    InvalidRoot,
    InvalidCharacter,
    InvalidSegment,
    MissingExtension
};

class FAssetPath
{
public:
    FAssetPath() = default;

    static bool TryParse(
        std::string_view Value,
        FAssetPath& OutPath,
        EAssetPathError* OutError = nullptr);

    bool IsValid() const;
    bool IsEmpty() const;
    std::string_view ToString() const;
    std::string_view GetGameRelativePath() const;
    std::string_view GetExtension() const;

    friend bool operator==(const FAssetPath&, const FAssetPath&) = default;
    friend bool operator<(const FAssetPath& Left, const FAssetPath& Right)
    {
        return Left.Value < Right.Value;
    }

private:
    explicit FAssetPath(std::string InValue);

    std::string Value;
};

std::string_view ToString(EAssetPathError Error);
}
