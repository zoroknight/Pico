#pragma once

#include <string_view>

namespace Pico
{
bool InitializeGameThread();
void ShutdownGameThread();
bool IsGameThreadInitialized();
bool IsInGameThread();
bool CheckGameThread(std::string_view Operation);
}
