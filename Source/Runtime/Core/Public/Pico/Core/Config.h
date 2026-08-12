#pragma once

#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Pico
{
class FConfigFile
{
public:
    bool Load(const std::filesystem::path& FilePath);
    bool Load(std::string_view FilePath);

    std::optional<std::string> GetString(std::string_view Section, std::string_view Key) const;
    std::string GetString(std::string_view Section, std::string_view Key, std::string_view DefaultValue) const;
    int GetInt(std::string_view Section, std::string_view Key, int DefaultValue) const;
    double GetDouble(std::string_view Section, std::string_view Key, double DefaultValue) const;
    bool GetBool(std::string_view Section, std::string_view Key, bool DefaultValue) const;
    std::vector<std::pair<std::string, std::string>> GetSectionEntries(
        std::string_view Section) const;
    void SetString(std::string Section, std::string Key, std::string Value);
    bool Remove(std::string_view Section, std::string_view Key);
    bool RemoveSection(std::string_view Section);
    bool Save(const std::filesystem::path& FilePath) const;

private:
    using FSection = std::unordered_map<std::string, std::string>;

    static std::string Trim(std::string_view Text);
    static std::string ToLower(std::string_view Text);

    std::unordered_map<std::string, FSection> Sections;
};
}
