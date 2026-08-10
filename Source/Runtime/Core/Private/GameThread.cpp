#include "Pico/Core/GameThread.h"

#include "Pico/Core/Log.h"

#include <mutex>
#include <thread>

namespace Pico
{
namespace
{
std::mutex GGameThreadMutex;
std::thread::id GGameThreadId;
bool GGameThreadInitialized = false;
}

bool InitializeGameThread()
{
    std::scoped_lock Lock(GGameThreadMutex);
    const std::thread::id CurrentThread = std::this_thread::get_id();
    if (GGameThreadInitialized)
    {
        return GGameThreadId == CurrentThread;
    }
    GGameThreadId = CurrentThread;
    GGameThreadInitialized = true;
    return true;
}

void ShutdownGameThread()
{
    std::scoped_lock Lock(GGameThreadMutex);
    if (GGameThreadInitialized && GGameThreadId == std::this_thread::get_id())
    {
        GGameThreadId = {};
        GGameThreadInitialized = false;
    }
}

bool IsGameThreadInitialized()
{
    std::scoped_lock Lock(GGameThreadMutex);
    return GGameThreadInitialized;
}

bool IsInGameThread()
{
    std::scoped_lock Lock(GGameThreadMutex);
    return GGameThreadInitialized && GGameThreadId == std::this_thread::get_id();
}

bool CheckGameThread(std::string_view Operation)
{
    if (IsInGameThread())
    {
        return true;
    }
    PICO_LOG(LogCore, Error, "{} must run on the Game Thread", Operation);
    return false;
}
}
