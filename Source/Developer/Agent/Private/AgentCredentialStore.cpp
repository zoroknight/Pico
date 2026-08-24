#include "Pico/Agent/AgentCredentialStore.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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
        if (OutError) *OutError = "Could not inspect the project API key file";
        return false;
    }
    if (!bExists) return true;
    if (OutConfig.Load(Path)) return true;
    if (OutError) *OutError = "Could not read the project API key file";
    return false;
}
}

bool FAgentCredentialStore::IsSupported()
{
    return !GetStoragePath().empty();
}

std::filesystem::path FAgentCredentialStore::GetStoragePath()
{
    std::filesystem::path Path;
    if (!FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Saved, "Agent/ApiKeys.ini", Path))
    {
        return {};
    }
    return Path;
}

bool FAgentCredentialStore::TryLoadApiKey(
    std::string_view ProviderId,
    std::string& OutApiKey,
    std::string* OutError)
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
        if (OutError) *OutError = "No project is open for local API key storage";
        return false;
    }
    std::error_code Error;
    if (!std::filesystem::exists(Path, Error))
    {
        if (Error && OutError) *OutError = "Could not inspect the project API key file";
        return false;
    }
    FConfigFile Config;
    if (!Config.Load(Path))
    {
        if (OutError) *OutError = "Could not read the project API key file";
        return false;
    }
    OutApiKey = Config.GetString("ApiKeys", ProviderId, "");
    return !OutApiKey.empty();
}

bool FAgentCredentialStore::SaveApiKey(
    std::string_view ProviderId,
    std::string_view ApiKey,
    std::string* OutError)
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
        if (OutError) *OutError = "No project is open for local API key storage";
        return false;
    }
    FConfigFile Config;
    if (!LoadExistingConfig(Path, Config, OutError)) return false;
    Config.SetString("ApiKeys", std::string(ProviderId), std::string(ApiKey));
    if (Config.Save(Path)) return true;
    if (OutError) *OutError = "Could not save the project API key file";
    return false;
}

bool FAgentCredentialStore::DeleteApiKey(
    std::string_view ProviderId,
    std::string* OutError)
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
        if (OutError) *OutError = "No project is open for local API key storage";
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
        if (OutError) *OutError = "Could not remove the project API key file";
        return false;
    }
    if (Config.Save(Path)) return true;
    if (OutError) *OutError = "Could not update the project API key file";
    return false;
}
}
