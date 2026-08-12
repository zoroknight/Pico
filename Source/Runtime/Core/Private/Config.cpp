#include "Pico/Core/Config.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <algorithm>

namespace Pico
{
bool FConfigFile::Load(std::string_view FilePath)
{
    return Load(std::filesystem::path(std::string(FilePath)));
}

bool FConfigFile::Load(const std::filesystem::path& FilePath)
{
    Sections.clear();

    std::ifstream File { FilePath };
    if (!File.is_open())
    {
        return false;
    }

    std::string CurrentSection;
    std::string Line;

    while (std::getline(File, Line))
    {
        std::string TrimmedLine = Trim(Line);
        if (TrimmedLine.empty() || TrimmedLine.starts_with("#") || TrimmedLine.starts_with(";"))
        {
            continue;
        }

        if (TrimmedLine.starts_with("[") && TrimmedLine.ends_with("]"))
        {
            CurrentSection = Trim(std::string_view(TrimmedLine).substr(1, TrimmedLine.size() - 2));
            Sections.try_emplace(CurrentSection);
            continue;
        }

        const std::size_t EqualsIndex = TrimmedLine.find('=');
        if (EqualsIndex == std::string::npos)
        {
            continue;
        }

        std::string Key = Trim(std::string_view(TrimmedLine).substr(0, EqualsIndex));
        std::string Value = Trim(std::string_view(TrimmedLine).substr(EqualsIndex + 1));

        if (!Key.empty())
        {
            Sections[CurrentSection][std::move(Key)] = std::move(Value);
        }
    }

    return true;
}

std::optional<std::string> FConfigFile::GetString(std::string_view Section, std::string_view Key) const
{
    const auto SectionIt = Sections.find(std::string(Section));
    if (SectionIt == Sections.end())
    {
        return std::nullopt;
    }

    const auto ValueIt = SectionIt->second.find(std::string(Key));
    if (ValueIt == SectionIt->second.end())
    {
        return std::nullopt;
    }

    return ValueIt->second;
}

std::string FConfigFile::GetString(std::string_view Section, std::string_view Key, std::string_view DefaultValue) const
{
    return GetString(Section, Key).value_or(std::string(DefaultValue));
}

int FConfigFile::GetInt(std::string_view Section, std::string_view Key, int DefaultValue) const
{
    const std::optional<std::string> Value = GetString(Section, Key);
    if (!Value.has_value())
    {
        return DefaultValue;
    }

    int Result = 0;
    const char* Begin = Value->data();
    const char* End = Begin + Value->size();
    const std::from_chars_result ParseResult = std::from_chars(Begin, End, Result);

    return ParseResult.ec == std::errc{} && ParseResult.ptr == End ? Result : DefaultValue;
}

double FConfigFile::GetDouble(std::string_view Section, std::string_view Key, double DefaultValue) const
{
    const std::optional<std::string> Value = GetString(Section, Key);
    if (!Value.has_value())
    {
        return DefaultValue;
    }

    double Result = 0.0;
    const char* Begin = Value->data();
    const char* End = Begin + Value->size();
    const std::from_chars_result ParseResult = std::from_chars(Begin, End, Result, std::chars_format::general);

    return ParseResult.ec == std::errc{} && ParseResult.ptr == End ? Result : DefaultValue;
}

bool FConfigFile::GetBool(std::string_view Section, std::string_view Key, bool DefaultValue) const
{
    const std::optional<std::string> Value = GetString(Section, Key);
    if (!Value.has_value())
    {
        return DefaultValue;
    }

    const std::string LowerValue = ToLower(*Value);
    if (LowerValue == "true" || LowerValue == "1" || LowerValue == "yes" || LowerValue == "on")
    {
        return true;
    }

    if (LowerValue == "false" || LowerValue == "0" || LowerValue == "no" || LowerValue == "off")
    {
        return false;
    }

    return DefaultValue;
}

std::vector<std::pair<std::string, std::string>> FConfigFile::GetSectionEntries(
    std::string_view Section) const
{
    const auto SectionIt = Sections.find(std::string(Section));
    if (SectionIt == Sections.end())
    {
        return {};
    }

    std::vector<std::pair<std::string, std::string>> Entries;
    Entries.reserve(SectionIt->second.size());
    for (const auto& [Key, Value] : SectionIt->second)
    {
        Entries.emplace_back(Key, Value);
    }
    return Entries;
}

void FConfigFile::SetString(std::string Section, std::string Key, std::string Value)
{
    if (!Section.empty() && !Key.empty()) Sections[std::move(Section)][std::move(Key)] = std::move(Value);
}

bool FConfigFile::Remove(std::string_view Section, std::string_view Key)
{
    const auto FoundSection = Sections.find(std::string(Section));
    return FoundSection != Sections.end() && FoundSection->second.erase(std::string(Key)) > 0;
}

bool FConfigFile::RemoveSection(std::string_view Section)
{
    return Sections.erase(std::string(Section)) > 0;
}

bool FConfigFile::Save(const std::filesystem::path& FilePath) const
{
    if (FilePath.empty()) return false;
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    if (Error) return false;
    std::filesystem::path Temporary = FilePath;
    Temporary += ".tmp";
    std::ofstream File(Temporary, std::ios::trunc);
    if (!File) return false;
    std::vector<std::string> SectionNames;
    SectionNames.reserve(Sections.size());
    for (const auto& [Name, Entries] : Sections) SectionNames.push_back(Name);
    std::sort(SectionNames.begin(), SectionNames.end());
    for (const std::string& SectionName : SectionNames)
    {
        File << '[' << SectionName << "]\n";
        std::vector<std::pair<std::string, std::string>> Entries(
            Sections.at(SectionName).begin(), Sections.at(SectionName).end());
        std::sort(Entries.begin(), Entries.end());
        for (const auto& [Key, Value] : Entries) File << Key << '=' << Value << '\n';
        File << '\n';
    }
    File.close();
    if (!File) return false;
    std::filesystem::rename(Temporary, FilePath, Error);
    if (!Error) return true;
    Error.clear();
    std::filesystem::remove(FilePath, Error);
    Error.clear();
    std::filesystem::rename(Temporary, FilePath, Error);
    return !Error;
}

std::string FConfigFile::Trim(std::string_view Text)
{
    std::size_t Begin = 0;
    while (Begin < Text.size() && std::isspace(static_cast<unsigned char>(Text[Begin])))
    {
        ++Begin;
    }

    std::size_t End = Text.size();
    while (End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1])))
    {
        --End;
    }

    return std::string(Text.substr(Begin, End - Begin));
}

std::string FConfigFile::ToLower(std::string_view Text)
{
    std::string Result;
    Result.reserve(Text.size());

    for (const char Character : Text)
    {
        Result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Character))));
    }

    return Result;
}
}
