#include "Pico/Editor/EditorRuntimeFreshness.h"

#include "Pico/Core/Paths.h"

#include <string_view>

namespace Pico
{
bool IsDevelopmentGameRuntimeStale(
    const std::filesystem::path& GameExecutable,
    std::string& OutDependency)
{
    OutDependency.clear();
    if (FPaths::IsStaged()) return false;
    std::error_code TimeError;
    const auto GameTime = std::filesystem::last_write_time(
        GameExecutable, TimeError);
    if (TimeError) return false;

#if defined(_WIN32)
    constexpr std::string_view LibraryExtension = ".lib";
#else
    constexpr std::string_view LibraryExtension = ".a";
#endif
    // This list contains only libraries linked into the standalone game.
    // Editor/Agent-only libraries such as PicoTasks must never block Play.
    static constexpr std::string_view RuntimeLibraries[] {
        "PicoCore", "PicoObject", "PicoAsset", "PicoInput", "PicoNetCore",
        "PicoGraph", "PicoPhysicsCore", "PicoPhysicsJolt", "PicoEngine",
        "PicoRender", "PicoGameplayAbilities", "PicoGameRuntime"
    };
    const std::filesystem::path Directory = GameExecutable.parent_path();
    for (std::string_view LibraryName : RuntimeLibraries)
    {
        const std::filesystem::path Library = Directory
            / (std::string(LibraryName) + std::string(LibraryExtension));
        if (!std::filesystem::is_regular_file(Library)) continue;
        const auto LibraryTime = std::filesystem::last_write_time(
            Library, TimeError);
        if (!TimeError && LibraryTime > GameTime)
        {
            OutDependency = Library.filename().string();
            return true;
        }
        TimeError.clear();
    }
    return false;
}
}
