#include "Pico/Core/ProjectDescriptor.h"

#include "Pico/Core/Config.h"

#include <cctype>
#include <system_error>
#include <utility>

namespace Pico
{
namespace
{
bool IsValidProjectName(const std::string& Name)
{
    if (Name.empty() || Name == "." || Name == "..")
    {
        return false;
    }

    for (const unsigned char Character : Name)
    {
        if (!std::isalnum(Character) && Character != '_' && Character != '-')
        {
            return false;
        }
    }
    return true;
}

void SetError(std::string* OutError, std::string Message)
{
    if (OutError != nullptr)
    {
        *OutError = std::move(Message);
    }
}
}

bool FProjectDescriptor::Load(
    const std::filesystem::path& FilePath,
    FProjectDescriptor& OutDescriptor,
    std::string* OutError)
{
    OutDescriptor = {};
    if (OutError != nullptr)
    {
        OutError->clear();
    }

    std::error_code ErrorCode;
    const std::filesystem::path CanonicalPath =
        std::filesystem::weakly_canonical(FilePath, ErrorCode);
    if (FilePath.extension() != ".pico"
        || ErrorCode
        || !std::filesystem::is_regular_file(CanonicalPath, ErrorCode))
    {
        SetError(OutError, "Project descriptor is not a readable .pico file");
        return false;
    }

    FConfigFile Config;
    if (!Config.Load(CanonicalPath))
    {
        SetError(OutError, "Project descriptor could not be loaded");
        return false;
    }

    const std::string Name = Config.GetString("Project", "Name", "");
    const int FileVersion = Config.GetInt("Project", "FileVersion", 0);
    if (!IsValidProjectName(Name))
    {
        SetError(OutError, "Project Name must contain only letters, digits, '_' or '-'");
        return false;
    }
    if (FileVersion != 1)
    {
        SetError(OutError, "Unsupported project descriptor FileVersion");
        return false;
    }

    OutDescriptor.FilePath = CanonicalPath;
    OutDescriptor.RootDir = CanonicalPath.parent_path();
    OutDescriptor.Name = Name;
    OutDescriptor.EngineVersion = Config.GetString("Project", "EngineVersion", "");
    OutDescriptor.FileVersion = FileVersion;
    return true;
}

const std::filesystem::path& FProjectDescriptor::GetFilePath() const
{
    return FilePath;
}

const std::filesystem::path& FProjectDescriptor::GetRootDir() const
{
    return RootDir;
}

const std::string& FProjectDescriptor::GetName() const
{
    return Name;
}

const std::string& FProjectDescriptor::GetEngineVersion() const
{
    return EngineVersion;
}

int FProjectDescriptor::GetFileVersion() const
{
    return FileVersion;
}
}
