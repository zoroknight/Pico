#include "Pico/Core/AssetPath.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Pico
{
namespace
{
void ReportError(EAssetPathError* OutError, EAssetPathError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}
}

FAssetPath::FAssetPath(std::string InValue)
    : Value(std::move(InValue))
{
}

bool FAssetPath::TryParse(
    std::string_view InValue,
    FAssetPath& OutPath,
    EAssetPathError* OutError)
{
    OutPath = {};
    ReportError(OutError, EAssetPathError::None);
    if (InValue.empty())
    {
        ReportError(OutError, EAssetPathError::Empty);
        return false;
    }

    std::string Normalized(InValue);
    std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
    constexpr std::string_view Root = "/Game/";
    if (!Normalized.starts_with(Root))
    {
        ReportError(OutError, EAssetPathError::InvalidRoot);
        return false;
    }

    for (const unsigned char Character : Normalized)
    {
        if (Character < 32 || Character == ':' || Character == '?' || Character == '*'
            || Character == '"' || Character == '<' || Character == '>' || Character == '|')
        {
            ReportError(OutError, EAssetPathError::InvalidCharacter);
            return false;
        }
    }

    std::size_t SegmentStart = Root.size();
    while (SegmentStart < Normalized.size())
    {
        const std::size_t Separator = Normalized.find('/', SegmentStart);
        const std::size_t SegmentEnd = Separator == std::string::npos
            ? Normalized.size() : Separator;
        const std::string_view Segment(
            Normalized.data() + SegmentStart,
            SegmentEnd - SegmentStart);
        if (Segment.empty() || Segment == "." || Segment == "..")
        {
            ReportError(OutError, EAssetPathError::InvalidSegment);
            return false;
        }
        if (Separator == std::string::npos)
        {
            break;
        }
        SegmentStart = Separator + 1;
    }

    const std::size_t LastSlash = Normalized.find_last_of('/');
    const std::size_t LastDot = Normalized.find_last_of('.');
    if (LastDot == std::string::npos
        || LastDot <= LastSlash + 1
        || LastDot + 1 >= Normalized.size())
    {
        ReportError(OutError, EAssetPathError::MissingExtension);
        return false;
    }

    OutPath = FAssetPath(std::move(Normalized));
    return true;
}

bool FAssetPath::IsValid() const
{
    return !Value.empty();
}

bool FAssetPath::IsEmpty() const
{
    return Value.empty();
}

std::string_view FAssetPath::ToString() const
{
    return Value;
}

std::string_view FAssetPath::GetGameRelativePath() const
{
    constexpr std::string_view Root = "/Game/";
    return Value.starts_with(Root)
        ? std::string_view(Value).substr(Root.size())
        : std::string_view {};
}

std::string_view FAssetPath::GetExtension() const
{
    const std::size_t Dot = Value.find_last_of('.');
    return Dot != std::string::npos
        ? std::string_view(Value).substr(Dot)
        : std::string_view {};
}

std::string_view ToString(EAssetPathError Error)
{
    switch (Error)
    {
    case EAssetPathError::None: return "None";
    case EAssetPathError::Empty: return "Empty";
    case EAssetPathError::InvalidRoot: return "InvalidRoot";
    case EAssetPathError::InvalidCharacter: return "InvalidCharacter";
    case EAssetPathError::InvalidSegment: return "InvalidSegment";
    case EAssetPathError::MissingExtension: return "MissingExtension";
    }
    return "Unknown";
}
}
