#include "Pico/Editor/AssetSemanticMetadataService.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::string HashText(std::string_view Text)
{
    std::uint64_t Hash = 14695981039346656037ull;
    for (const unsigned char Byte : Text)
    {
        Hash ^= Byte;
        Hash *= 1099511628211ull;
    }
    std::ostringstream Stream;
    Stream << std::hex << std::setfill('0') << std::setw(16) << Hash;
    return Stream.str();
}

std::string HashFile(const std::filesystem::path& File)
{
    std::ifstream Stream(File, std::ios::binary);
    if (!Stream) return {};
    std::ostringstream Bytes;
    Bytes << Stream.rdbuf();
    return Stream ? HashText(Bytes.str()) : std::string {};
}

bool IsSafeText(std::string_view Text, std::size_t Maximum)
{
    if (Text.size() > Maximum) return false;
    return std::none_of(Text.begin(), Text.end(), [](unsigned char Character)
    {
        return Character < 0x20 && Character != '\n' && Character != '\t';
    });
}

bool ValidateList(
    const std::vector<std::string>& Values,
    std::string_view Name,
    std::string* OutError)
{
    if (Values.size() > 32)
    {
        if (OutError) *OutError = std::string(Name) + " may contain at most 32 entries";
        return false;
    }
    std::set<std::string> Unique;
    for (const std::string& Value : Values)
    {
        if (Value.empty() || !IsSafeText(Value, 64) || !Unique.insert(Value).second)
        {
            if (OutError) *OutError = std::string(Name)
                + " entries must be unique non-empty strings up to 64 bytes";
            return false;
        }
    }
    return true;
}

FJson ToJson(const FAssetSemanticMetadata& Metadata)
{
    return {{"schema_version", Metadata.SchemaVersion},
        {"display_name", Metadata.DisplayName},
        {"description", Metadata.Description},
        {"semantic_tags", Metadata.SemanticTags},
        {"intended_use", Metadata.IntendedUse},
        {"surface_tags", Metadata.SurfaceTags},
        {"provenance", Metadata.Provenance},
        {"source_asset_revision", Metadata.SourceAssetRevision}};
}

bool FromJson(
    const FJson& Json,
    FAssetSemanticMetadata& OutMetadata,
    std::string& OutError)
{
    if (!Json.is_object()
        || Json.value("schema_version", 0)
            != FAssetSemanticMetadata::CurrentSchemaVersion)
    {
        OutError = "Unsupported or missing semantic metadata schema_version";
        return false;
    }
    try
    {
        FAssetSemanticMetadata Metadata;
        Metadata.SchemaVersion = Json.at("schema_version").get<int>();
        Metadata.DisplayName = Json.value("display_name", "");
        Metadata.Description = Json.value("description", "");
        Metadata.SemanticTags = Json.value(
            "semantic_tags", std::vector<std::string> {});
        Metadata.IntendedUse = Json.value(
            "intended_use", std::vector<std::string> {});
        Metadata.SurfaceTags = Json.value(
            "surface_tags", std::vector<std::string> {});
        Metadata.Provenance = Json.value("provenance", "");
        Metadata.SourceAssetRevision = Json.value("source_asset_revision", "");
        if (!FAssetSemanticMetadataService::Validate(Metadata, &OutError))
            return false;
        OutMetadata = std::move(Metadata);
        return true;
    }
    catch (const std::exception& Exception)
    {
        OutError = std::string("Invalid semantic metadata: ") + Exception.what();
        return false;
    }
}

bool PublishFile(
    const std::filesystem::path& Temporary,
    const std::filesystem::path& Destination,
    std::string& OutError)
{
    std::error_code Error;
    const std::filesystem::path Backup = Destination.string() + ".bak";
    std::filesystem::remove(Backup, Error);
    Error.clear();
    const bool bHadDestination = std::filesystem::is_regular_file(Destination, Error);
    Error.clear();
    if (bHadDestination)
    {
        std::filesystem::rename(Destination, Backup, Error);
        if (Error)
        {
            OutError = "Could not stage the previous semantic metadata";
            return false;
        }
    }
    std::filesystem::rename(Temporary, Destination, Error);
    if (Error)
    {
        if (bHadDestination)
        {
            std::error_code RestoreError;
            std::filesystem::rename(Backup, Destination, RestoreError);
        }
        OutError = "Could not publish semantic metadata";
        return false;
    }
    std::filesystem::remove(Backup, Error);
    return true;
}
}

std::filesystem::path FAssetSemanticMetadataService::GetSidecarPath(
    const std::filesystem::path& AssetFile)
{
    return AssetFile.string() + ".pmeta.json";
}

std::string FAssetSemanticMetadataService::ComputeAssetRevision(
    const std::filesystem::path& AssetFile)
{
    return HashFile(AssetFile);
}

bool FAssetSemanticMetadataService::Validate(
    const FAssetSemanticMetadata& Metadata,
    std::string* OutError)
{
    if (Metadata.SchemaVersion != FAssetSemanticMetadata::CurrentSchemaVersion)
    {
        if (OutError) *OutError = "Unsupported semantic metadata schema version";
        return false;
    }
    if (!IsSafeText(Metadata.DisplayName, 128)
        || !IsSafeText(Metadata.Description, 2048))
    {
        if (OutError) *OutError = "Display name or description is too long or invalid";
        return false;
    }
    if (Metadata.Provenance != "user-confirmed")
    {
        if (OutError) *OutError = "Formal semantic metadata provenance must be user-confirmed";
        return false;
    }
    return ValidateList(Metadata.SemanticTags, "semantic_tags", OutError)
        && ValidateList(Metadata.IntendedUse, "intended_use", OutError)
        && ValidateList(Metadata.SurfaceTags, "surface_tags", OutError)
        && IsSafeText(Metadata.SourceAssetRevision, 64);
}

FAssetSemanticMetadataResult FAssetSemanticMetadataService::Load(
    const std::filesystem::path& AssetFile,
    FAssetSemanticMetadata& OutMetadata)
{
    const std::filesystem::path Sidecar = GetSidecarPath(AssetFile);
    std::error_code FileError;
    if (!std::filesystem::is_regular_file(Sidecar, FileError))
    {
        OutMetadata = {};
        return {true, false, "Asset has no semantic metadata", "none"};
    }
    try
    {
        std::ifstream Stream(Sidecar, std::ios::binary);
        FJson Json;
        Stream >> Json;
        if (!Stream) return {false, true, "Could not read semantic metadata", {}};
        std::string Error;
        if (!FromJson(Json, OutMetadata, Error))
            return {false, true, std::move(Error), {}};
        const std::string Canonical = ToJson(OutMetadata).dump();
        return {true, true, "Loaded semantic metadata", HashText(Canonical)};
    }
    catch (const std::exception& Exception)
    {
        return {false, true,
            std::string("Could not parse semantic metadata: ") + Exception.what(), {}};
    }
}

FAssetSemanticMetadataResult FAssetSemanticMetadataService::Save(
    const std::filesystem::path& AssetFile,
    const FAssetSemanticMetadata& Metadata)
{
    std::error_code FileError;
    if (!std::filesystem::is_regular_file(AssetFile, FileError))
        return {false, false, "Cannot save metadata for a missing asset", {}};
    std::string Error;
    if (!Validate(Metadata, &Error)) return {false, false, std::move(Error), {}};
    const std::filesystem::path Sidecar = GetSidecarPath(AssetFile);
    std::filesystem::create_directories(Sidecar.parent_path(), FileError);
    if (FileError) return {false, false, "Could not create metadata directory", {}};
    const std::string Canonical = ToJson(Metadata).dump();
    const std::filesystem::path Temporary = Sidecar.string() + ".tmp";
    {
        std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
        Stream << ToJson(Metadata).dump(2) << '\n';
        Stream.flush();
        if (!Stream)
        {
            std::filesystem::remove(Temporary, FileError);
            return {false, false, "Could not stage semantic metadata", {}};
        }
    }
    if (!PublishFile(Temporary, Sidecar, Error))
    {
        std::filesystem::remove(Temporary, FileError);
        return {false, false, std::move(Error), {}};
    }
    return {true, true, "Saved semantic metadata", HashText(Canonical)};
}

FAssetSemanticMetadataResult FAssetSemanticMetadataService::Move(
    const std::filesystem::path& OldAssetFile,
    const std::filesystem::path& NewAssetFile)
{
    const std::filesystem::path OldSidecar = GetSidecarPath(OldAssetFile);
    const std::filesystem::path NewSidecar = GetSidecarPath(NewAssetFile);
    std::error_code Error;
    if (!std::filesystem::is_regular_file(OldSidecar, Error))
        return {true, false, "Asset has no semantic metadata to move", "none"};
    if (std::filesystem::exists(NewSidecar, Error))
        return {false, true, "Destination semantic metadata already exists", {}};
    std::filesystem::rename(OldSidecar, NewSidecar, Error);
    if (Error) return {false, true, "Could not move semantic metadata", {}};
    FAssetSemanticMetadata Metadata;
    return Load(NewAssetFile, Metadata);
}

FAssetSemanticMetadataResult FAssetSemanticMetadataService::Copy(
    const std::filesystem::path& SourceAssetFile,
    const std::filesystem::path& DestinationAssetFile)
{
    FAssetSemanticMetadata Metadata;
    const FAssetSemanticMetadataResult Loaded = Load(SourceAssetFile, Metadata);
    if (!Loaded.bSucceeded || !Loaded.bExists) return Loaded;
    Metadata.SourceAssetRevision = HashFile(DestinationAssetFile);
    return Save(DestinationAssetFile, Metadata);
}
}
