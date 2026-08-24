#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Pico
{
class FAgentCredentialStore
{
public:
    static bool IsSupported();
    static std::filesystem::path GetStoragePath();
    static bool TryLoadApiKey(
        std::string_view ProviderId,
        std::string& OutApiKey,
        std::string* OutError = nullptr);
    static bool SaveApiKey(
        std::string_view ProviderId,
        std::string_view ApiKey,
        std::string* OutError = nullptr);
    static bool DeleteApiKey(
        std::string_view ProviderId,
        std::string* OutError = nullptr);
};
}
