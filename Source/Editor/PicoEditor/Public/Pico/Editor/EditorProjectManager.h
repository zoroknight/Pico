#pragma once

#include "Pico/Core/AssetPath.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Pico
{
struct FEditorProjectResolution
{
    std::filesystem::path ProjectFile;
    std::vector<std::filesystem::path> Candidates;
    std::string Message;

    bool IsResolved() const { return !ProjectFile.empty(); }
};

FEditorProjectResolution ResolveEditorProjectPath(
    const std::filesystem::path& Selection);
std::filesystem::path GetEditorUserSettingsFile();

class FEditorProjectHistory
{
public:
    static constexpr std::size_t MaxRecentProjects = 8;

    bool Load(const std::filesystem::path& FilePath);
    bool Save(const std::filesystem::path& FilePath) const;
    void Add(const std::filesystem::path& ProjectFile);
    void Remove(const std::filesystem::path& ProjectFile);

    const std::vector<std::filesystem::path>& GetRecentProjects() const
    {
        return RecentProjects;
    }

private:
    std::vector<std::filesystem::path> RecentProjects;
};

struct FEditorSessionState
{
    FAssetPath LastWorld;
    FAssetPath OpenActorBlueprint;
    FAssetPath OpenSkeletalAsset;
    bool bAgentChatOpen = true;
    bool bExternalAgentsOpen = false;

    bool Load(const std::filesystem::path& FilePath);
    bool Save(const std::filesystem::path& FilePath) const;
};
}
