#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Pico
{
class FAgentCredentialStore
{
public:
    FAgentCredentialStore() = default;
    explicit FAgentCredentialStore(std::filesystem::path StoragePathOverride);
    FAgentCredentialStore(
        std::filesystem::path StoragePathOverride,
        std::filesystem::path LegacyProjectPathOverride);

    bool IsSupported() const;
    std::filesystem::path GetStoragePath() const;
    bool TryLoadApiKey(
        std::string_view ProviderId,
        std::string& OutApiKey,
        std::string* OutError = nullptr) const;
    bool SaveApiKey(
        std::string_view ProviderId,
        std::string_view ApiKey,
        std::string* OutError = nullptr) const;
    bool DeleteApiKey(
        std::string_view ProviderId,
        std::string* OutError = nullptr) const;

private:
    bool TryMigrateProjectCredential(
        std::string_view ProviderId,
        std::string& OutApiKey,
        std::string* OutError) const;

    std::filesystem::path StoragePathOverride;
    std::filesystem::path LegacyProjectPathOverride;
};
}
