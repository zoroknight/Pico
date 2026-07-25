#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Pico
{
class FApp
{
public:
    static void Init(std::string_view InProjectName);
    static void RequestExit();

    static bool IsExitRequested();
    static std::string_view GetProjectName();
    static std::uint64_t GetFrameCounter();

    static void BeginFrame();

private:
    static inline bool bExitRequested = false;
    static inline std::uint64_t FrameCounter = 0;
    static inline std::string ProjectName = "Pico";
};
}
