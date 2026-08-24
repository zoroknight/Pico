#include "Pico/Agent/AgentCredentialStore.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <utility>

namespace Pico
{
namespace
{
bool IsValidProviderId(std::string_view ProviderId)
{
    if (ProviderId.empty() || ProviderId.size() > 32) return false;
    return std::all_of(ProviderId.begin(), ProviderId.end(), [](unsigned char Character)
    {
        return std::isalnum(Character) || Character == '-' || Character == '_';
    });
}

bool IsValidApiKey(std::string_view ApiKey)
{
    return ApiKey.size() >= 8 && ApiKey.size() <= 512
        && ApiKey.find_first_of("\r\n") == std::string_view::npos;
}

bool LoadExistingConfig(
    const std::filesystem::path& Path,
    FConfigFile& OutConfig,
    std::string* OutError)
{
    std::error_code Error;
    const bool bExists = std::filesystem::exists(Path, Error);
    if (Error)
    {
        if (OutError) *OutError = "Could not inspect the local API key file";
        return false;
    }
    if (!bExists) return true;
    if (OutConfig.Load(Path)) return true;
    if (OutError) *OutError = "Could not read the local API key file";
    return false;
}

bool SaveConfigValue(
    const std::filesystem::path& Path,
    std::string_view ProviderId,
    std::string_view ApiKey,
    std::string* OutError)
{
    FConfigFile Config;
    if (!LoadExistingConfig(Path, Config, OutError)) return false;
    Config.SetString("ApiKeys", std::string(ProviderId), std::string(ApiKey));
    if (Config.Save(Path)) return true;
    if (OutError) *OutError = "Could not save the editor-local API key file";
    return false;
}

std::filesystem::path LegacyProjectStoragePath()
{
    std::filesystem::path Path;
    if (!FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Saved, "Agent/ApiKeys.ini", Path))
    {
        return {};
    }
    return Path;
}
}

FAgentCredentialStore::FAgentCredentialStore(
    std::filesystem::path InStoragePathOverride)
    : StoragePathOverride(std::move(InStoragePathOverride))
{
}

FAgentCredentialStore::FAgentCredentialStore(
    std::filesystem::path InStoragePathOverride,
    std::filesystem::path InLegacyProjectPathOverride)
    : StoragePathOverride(std::move(InStoragePathOverride)),
      LegacyProjectPathOverride(std::move(InLegacyProjectPathOverride))
{
}

bool FAgentCredentialStore::IsSupported() const
{
    return !GetStoragePath().empty();
}

std::filesystem::path FAgentCredentialStore::GetStoragePath() const
{
    if (!StoragePathOverride.empty()) return StoragePathOverride;
    const std::filesystem::path& EngineRoot = FPaths::GetEngineRootDir();
    return EngineRoot.empty()
        ? std::filesystem::path {}
        : EngineRoot / "Saved/Editor/Agent/ApiKeys.ini";
}

bool FAgentCredentialStore::TryLoadApiKey(
    std::string_view ProviderId,
    std::string& OutApiKey,
    std::string* OutError) const
{
    OutApiKey.clear();
    if (OutError) OutError->clear();
    if (!IsValidProviderId(ProviderId))
    {
        if (OutError) *OutError = "Provider credential id is invalid";
        return false;
    }
    const std::filesystem::path Path = GetStoragePath();
    if (Path.empty())
    {
        if (OutError) *OutError = "The Pico editor root is unavailable for local API key storage";
        return false;
    }
    std::error_code Error;
    if (!std::filesystem::exists(Path, Error))
    {
        if (Error)
        {
            if (OutError) *OutError = "Could not inspect the editor-local API key file";
            return false;
        }
        return TryMigrateProjectCredential(ProviderId, OutApiKey, OutError);
    }
    FConfigFile Config;
    if (!Config.Load(Path))
    {
        if (OutError) *OutError = "Could not read the editor-local API key file";
        return false;
    }
    OutApiKey = Config.GetString("ApiKeys", ProviderId, "");
    if (!OutApiKey.empty()) return true;
    return TryMigrateProjectCredential(ProviderId, OutApiKey, OutError);
}

bool FAgentCredentialStore::SaveApiKey(
    std::string_view ProviderId,
    std::string_view ApiKey,
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (!IsValidProviderId(ProviderId))
    {
        if (OutError) *OutError = "Provider credential id is invalid";
        return false;
    }
    if (!IsValidApiKey(ApiKey))
    {
        if (OutError) *OutError = "API key must contain 8 to 512 bytes and no newline";
        return false;
    }
    const std::filesystem::path Path = GetStoragePath();
    if (Path.empty())
    {
        if (OutError) *OutError = "The Pico editor root is unavailable for local API key storage";
        return false;
    }
    return SaveConfigValue(Path, ProviderId, ApiKey, OutError);
}

bool FAgentCredentialStore::DeleteApiKey(
    std::string_view ProviderId,
    std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (!IsValidProviderId(ProviderId))
    {
        if (OutError) *OutError = "Provider credential id is invalid";
        return false;
    }
    const std::filesystem::path Path = GetStoragePath();
    if (Path.empty())
    {
        if (OutError) *OutError = "The Pico editor root is unavailable for local API key storage";
        return false;
    }
    std::error_code Error;
    if (!std::filesystem::exists(Path, Error)) return !Error;
    FConfigFile Config;
    if (!LoadExistingConfig(Path, Config, OutError)) return false;
    Config.Remove("ApiKeys", ProviderId);
    if (Config.GetSectionEntries("ApiKeys").empty())
    {
        std::filesystem::remove(Path, Error);
        if (!Error) return true;
        if (OutError) *OutError = "Could not remove the editor-local API key file";
        return false;
    }
    if (Config.Save(Path)) return true;
    if (OutError) *OutError = "Could not update the editor-local API key file";
    return false;
}

bool FAgentCredentialStore::TryMigrateProjectCredential(
    std::string_view ProviderId,
    std::string& OutApiKey,
    std::string* OutError) const
{
    // An isolated store only migrates from an explicitly supplied legacy fixture.
    const std::filesystem::path LegacyPath = !LegacyProjectPathOverride.empty()
        ? LegacyProjectPathOverride
        : (StoragePathOverride.empty()
            ? LegacyProjectStoragePath()
            : std::filesystem::path {});
    if (LegacyPath.empty() || LegacyPath == GetStoragePath()) return false;

    std::error_code Error;
    if (!std::filesystem::exists(LegacyPath, Error)) return false;
    if (Error)
    {
        if (OutError) *OutError = "Could not inspect the legacy project API key file";
        return false;
    }

    FConfigFile LegacyConfig;
    if (!LegacyConfig.Load(LegacyPath))
    {
        if (OutError) *OutError = "Could not read the legacy project API key file";
        return false;
    }
    OutApiKey = LegacyConfig.GetString("ApiKeys", ProviderId, "");
    if (OutApiKey.empty()) return false;
    if (!SaveConfigValue(GetStoragePath(), ProviderId, OutApiKey, OutError))
    {
        OutApiKey.clear();
        return false;
    }

    LegacyConfig.Remove("ApiKeys", ProviderId);
    if (LegacyConfig.GetSectionEntries("ApiKeys").empty())
    {
        std::filesystem::remove(LegacyPath, Error);
    }
    else if (!LegacyConfig.Save(LegacyPath))
    {
        Error = std::make_error_code(std::errc::io_error);
    }
    if (Error && OutError)
    {
        *OutError = "API key migrated, but the legacy project copy could not be removed";
    }
    return true;
}
}
