#include "Pico/Core/App.h"

namespace Pico
{
void FApp::Init(std::string_view InProjectName)
{
    ProjectName = InProjectName;
    bExitRequested = false;
    FrameCounter = 0;
}

void FApp::RequestExit()
{
    bExitRequested = true;
}

bool FApp::IsExitRequested()
{
    return bExitRequested;
}

std::string_view FApp::GetProjectName()
{
    return ProjectName;
}

std::uint64_t FApp::GetFrameCounter()
{
    return FrameCounter;
}

void FApp::BeginFrame()
{
    ++FrameCounter;
}
}
