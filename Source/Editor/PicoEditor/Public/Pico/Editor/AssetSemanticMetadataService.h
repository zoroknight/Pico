#pragma once

#include "Pico/Core/AssetPath.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
struct FAssetSemanticMetadata
{
    static constexpr int CurrentSchemaVersion = 1;

    int SchemaVersion = CurrentSchemaVersion;
    std::string DisplayName;
    std::string Description;
    std::vector<std::string> SemanticTags;
    std::vector<std::string> IntendedUse;
    std::vector<std::string> SurfaceTags;
    std::string Provenance = "user-confirmed";
    std::string SourceAssetRevision;

    friend bool operator==(
        const FAssetSemanticMetadata&,
        const FAssetSemanticMetadata&) = default;
};

struct FAssetSemanticMetadataResult
{
    bool bSucceeded = false;
    bool bExists = false;
    std::string Message;
    std::string Revision;
};

class FAssetSemanticMetadataService
{
public:
    static std::filesystem::path GetSidecarPath(
        const std::filesystem::path& AssetFile);
    static std::string ComputeAssetRevision(
        const std::filesystem::path& AssetFile);
    static bool Validate(
        const FAssetSemanticMetadata& Metadata,
        std::string* OutError = nullptr);
    static FAssetSemanticMetadataResult Load(
        const std::filesystem::path& AssetFile,
        FAssetSemanticMetadata& OutMetadata);
    static FAssetSemanticMetadataResult Save(
        const std::filesystem::path& AssetFile,
        const FAssetSemanticMetadata& Metadata);
    static FAssetSemanticMetadataResult Move(
        const std::filesystem::path& OldAssetFile,
        const std::filesystem::path& NewAssetFile);
    static FAssetSemanticMetadataResult Copy(
        const std::filesystem::path& SourceAssetFile,
        const std::filesystem::path& DestinationAssetFile);
};
}
