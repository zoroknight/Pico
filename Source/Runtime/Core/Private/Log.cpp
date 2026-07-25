#include "Pico/Core/Log.h"

#include <chrono>
#include <iomanip>
#include <mutex>

namespace Pico
{
namespace
{
std::mutex GLogMutex;

std::string_view ToString(ELogLevel Level)
{
    switch (Level)
    {
    case ELogLevel::Trace:
        return "Trace";
    case ELogLevel::Info:
        return "Info";
    case ELogLevel::Warning:
        return "Warning";
    case ELogLevel::Error:
        return "Error";
    }

    return "Unknown";
}
}

void FLog::Write(ELogLevel Level, std::string_view Message)
{
    Write(DefaultCategory, Level, Message);
}

void FLog::Write(std::string_view Category, ELogLevel Level, std::string_view Message)
{
    const std::scoped_lock Lock(GLogMutex);
    const auto Now = std::chrono::system_clock::now();
    const auto Time = std::chrono::system_clock::to_time_t(Now);

    std::tm LocalTime {};
#if defined(_WIN32)
    localtime_s(&LocalTime, &Time);
#else
    localtime_r(&Time, &LocalTime);
#endif

    std::ostream& Stream = Level == ELogLevel::Error ? std::cerr : std::cout;
    Stream << "[" << std::put_time(&LocalTime, "%H:%M:%S") << "]";
    Stream << "[" << Category << "][" << ToString(Level) << "] " << Message << '\n';
}
}
