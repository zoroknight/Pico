#pragma once

#include <filesystem>
#include <string>

namespace Pico
{
class FProjectDescriptor
{
public:
    static bool Load(
        const std::filesystem::path& FilePath,
        FProjectDescriptor& OutDescriptor,
        std::string* OutError = nullptr);

    const std::filesystem::path& GetFilePath() const;
    const std::filesystem::path& GetRootDir() const;
    const std::string& GetName() const;
    const std::string& GetEngineVersion() const;
    int GetFileVersion() const;

private:
    std::filesystem::path FilePath;
    std::filesystem::path RootDir;
    std::string Name;
    std::string EngineVersion;
    int FileVersion = 0;
};
}
