#include "Pico/Asset/AssetRegistry.h"

#include "Pico/Core/Paths.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Pico
{
namespace
{
std::string ToLower(std::string_view Value)
{
    std::string Result(Value);
    std::transform(
        Result.begin(),
        Result.end(),
        Result.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    return Result;
}

std::optional<EAssetType> GetAssetType(const std::filesystem::path& FilePath)
{
    const std::string Extension = ToLower(FilePath.extension().string());
    if (Extension == ".pworld") return EAssetType::World;
    if (Extension == ".pmesh") return EAssetType::StaticMesh;
    if (Extension == ".ptex") return EAssetType::Texture;
    if (Extension == ".pmat") return EAssetType::Material;
    if (Extension == ".pskeleton") return EAssetType::Skeleton;
    if (Extension == ".pskeletalmesh") return EAssetType::SkeletalMesh;
    if (Extension == ".panimation") return EAssetType::AnimationClip;
    if (Extension == ".panimset") return EAssetType::AnimationSet;
    if (Extension == ".pmontage") return EAssetType::AnimationMontage;
    if (Extension == ".pcharprofile") return EAssetType::CharacterProfile;
    if (Extension == ".pcontrolprofile") return EAssetType::ThirdPersonControlProfile;
    if (Extension == ".pblueprint") return EAssetType::ActorBlueprint;
    if (Extension == ".pgraph") return EAssetType::PicoGraph;
    return std::nullopt;
}

bool TryMakeAssetPath(
    const std::filesystem::path& ContentRoot,
    const std::filesystem::path& FilePath,
    FAssetPath& OutAssetPath)
{
    std::error_code ErrorCode;
    const std::filesystem::path Relative =
        std::filesystem::relative(FilePath, ContentRoot, ErrorCode);
    if (ErrorCode || Relative.empty() || Relative.is_absolute())
    {
        return false;
    }
    const std::string RelativeString = Relative.generic_string();
    return FAssetPath::TryParse("/Game/" + RelativeString, OutAssetPath);
}
}

bool FAssetRegistry::ScanProjectContent(FAssetScanReport* OutReport)
{
    FAssetScanReport Report;
    if (!FPaths::HasProject())
    {
        Clear();
        if (OutReport != nullptr)
        {
            *OutReport = std::move(Report);
        }
        return true;
    }

    const std::filesystem::path ContentRoot = FPaths::GetProjectContentDir();
    std::error_code ErrorCode;
    if (!std::filesystem::exists(ContentRoot, ErrorCode))
    {
        Clear();
        if (OutReport != nullptr)
        {
            *OutReport = std::move(Report);
        }
        return !ErrorCode;
    }
    if (ErrorCode || !std::filesystem::is_directory(ContentRoot, ErrorCode))
    {
        Report.Issues.push_back(
            FAssetScanIssue { EAssetScanError::ContentUnavailable, ContentRoot });
        if (OutReport != nullptr)
        {
            *OutReport = std::move(Report);
        }
        return false;
    }

    std::vector<std::filesystem::path> Files;
    std::filesystem::recursive_directory_iterator Iterator(
        ContentRoot,
        std::filesystem::directory_options::skip_permission_denied,
        ErrorCode);
    const std::filesystem::recursive_directory_iterator End;
    if (ErrorCode)
    {
        Report.Issues.push_back(
            FAssetScanIssue { EAssetScanError::ContentUnavailable, ContentRoot });
        if (OutReport != nullptr)
        {
            *OutReport = std::move(Report);
        }
        return false;
    }

    while (Iterator != End)
    {
        const std::filesystem::directory_entry Entry = *Iterator;
        const std::filesystem::file_status Status = Entry.symlink_status(ErrorCode);
        if (ErrorCode)
        {
            Report.Issues.push_back(
                FAssetScanIssue { EAssetScanError::TraversalFailed, Entry.path() });
            ErrorCode.clear();
        }
        else if (std::filesystem::is_symlink(Status))
        {
            if (Entry.is_directory(ErrorCode))
            {
                Iterator.disable_recursion_pending();
            }
            ErrorCode.clear();
        }
        else if (std::filesystem::is_regular_file(Status))
        {
            Files.push_back(Entry.path());
        }

        Iterator.increment(ErrorCode);
        if (ErrorCode)
        {
            Report.Issues.push_back(
                FAssetScanIssue { EAssetScanError::TraversalFailed, Entry.path() });
            ErrorCode.clear();
        }
    }

    std::sort(Files.begin(), Files.end());
    std::vector<FAssetRecord> Candidates;
    for (const std::filesystem::path& FilePath : Files)
    {
        ++Report.ScannedFileCount;
        const std::optional<EAssetType> Type = GetAssetType(FilePath);
        if (!Type.has_value())
        {
            ++Report.IgnoredFileCount;
            continue;
        }

        FAssetPath AssetPath;
        if (!TryMakeAssetPath(ContentRoot, FilePath, AssetPath))
        {
            Report.Issues.push_back(
                FAssetScanIssue { EAssetScanError::InvalidAssetPath, FilePath });
            continue;
        }

        ErrorCode.clear();
        const std::uintmax_t FileSize = std::filesystem::file_size(FilePath, ErrorCode);
        if (ErrorCode)
        {
            Report.Issues.push_back(
                FAssetScanIssue { EAssetScanError::TraversalFailed, FilePath });
            continue;
        }
        const std::filesystem::file_time_type LastWriteTime =
            std::filesystem::last_write_time(FilePath, ErrorCode);
        if (ErrorCode)
        {
            Report.Issues.push_back(
                FAssetScanIssue { EAssetScanError::TraversalFailed, FilePath });
            continue;
        }

        ErrorCode.clear();
        const std::filesystem::path CanonicalFilePath =
            std::filesystem::weakly_canonical(FilePath, ErrorCode);
        if (ErrorCode || CanonicalFilePath.empty())
        {
            Report.Issues.push_back(
                FAssetScanIssue { EAssetScanError::TraversalFailed, FilePath });
            continue;
        }

        Candidates.push_back(FAssetRecord {
            std::move(AssetPath),
            CanonicalFilePath,
            *Type,
            FileSize,
            LastWriteTime
        });
    }

    std::sort(
        Candidates.begin(),
        Candidates.end(),
        [](const FAssetRecord& Left, const FAssetRecord& Right)
        {
            return Left.AssetPath < Right.AssetPath;
        });
    std::unordered_map<std::string, std::size_t> ComparisonCounts;
    for (const FAssetRecord& Candidate : Candidates)
    {
        ++ComparisonCounts[ToLower(Candidate.AssetPath.ToString())];
    }

    std::vector<FAssetRecord> NewAssets;
    for (FAssetRecord& Candidate : Candidates)
    {
        if (ComparisonCounts[ToLower(Candidate.AssetPath.ToString())] != 1)
        {
            Report.Issues.push_back(FAssetScanIssue {
                EAssetScanError::DuplicateAssetPath,
                Candidate.FilePath
            });
            continue;
        }
        NewAssets.push_back(std::move(Candidate));
    }

    std::vector<std::pair<std::string, std::size_t>> NewLookup;
    NewLookup.reserve(NewAssets.size());
    for (std::size_t Index = 0; Index < NewAssets.size(); ++Index)
    {
        NewLookup.emplace_back(
            ToLower(NewAssets[Index].AssetPath.ToString()),
            Index);
    }
    std::sort(NewLookup.begin(), NewLookup.end());

    Report.RegisteredAssetCount = NewAssets.size();
    Assets = std::move(NewAssets);
    Lookup = std::move(NewLookup);
    if (OutReport != nullptr)
    {
        *OutReport = std::move(Report);
    }
    return true;
}

const FAssetRecord* FAssetRegistry::Find(const FAssetPath& AssetPath) const
{
    if (!AssetPath.IsValid())
    {
        return nullptr;
    }
    const std::string Key = ToLower(AssetPath.ToString());
    const auto Found = std::lower_bound(
        Lookup.begin(),
        Lookup.end(),
        Key,
        [](const auto& Entry, const std::string& Value)
        {
            return Entry.first < Value;
        });
    return Found != Lookup.end() && Found->first == Key
        ? &Assets[Found->second]
        : nullptr;
}

std::span<const FAssetRecord> FAssetRegistry::GetAssets() const
{
    return Assets;
}

void FAssetRegistry::Clear()
{
    Assets.clear();
    Lookup.clear();
}

std::string_view ToString(EAssetType Type)
{
    switch (Type)
    {
    case EAssetType::World: return "World";
    case EAssetType::StaticMesh: return "StaticMesh";
    case EAssetType::Texture: return "Texture";
    case EAssetType::Material: return "Material";
    case EAssetType::Skeleton: return "Skeleton";
    case EAssetType::SkeletalMesh: return "SkeletalMesh";
    case EAssetType::AnimationClip: return "AnimationClip";
    case EAssetType::AnimationSet: return "AnimationSet";
    case EAssetType::AnimationMontage: return "AnimationMontage";
    case EAssetType::CharacterProfile: return "CharacterProfile";
    case EAssetType::ThirdPersonControlProfile: return "ThirdPersonControlProfile";
    case EAssetType::ActorBlueprint: return "ActorBlueprint";
    case EAssetType::PicoGraph: return "PicoGraph";
    }
    return "Unknown";
}

std::string_view ToString(EAssetScanError Error)
{
    switch (Error)
    {
    case EAssetScanError::ContentUnavailable: return "ContentUnavailable";
    case EAssetScanError::TraversalFailed: return "TraversalFailed";
    case EAssetScanError::InvalidAssetPath: return "InvalidAssetPath";
    case EAssetScanError::DuplicateAssetPath: return "DuplicateAssetPath";
    }
    return "Unknown";
}
}
