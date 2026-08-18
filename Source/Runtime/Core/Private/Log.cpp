#include "Pico/Core/Log.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace Pico
{
namespace
{
std::mutex GLogMutex;
std::ofstream GLogFile;
bool GConsoleOutputEnabled = true;
std::uint64_t GLogSequence = 0;
std::deque<FLogRecord> GLogHistory;
constexpr std::size_t MaxLogHistory = 512;

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
    const auto Now = std::chrono::system_clock::now();
    const auto Time = std::chrono::system_clock::to_time_t(Now);

    std::tm LocalTime {};
#if defined(_WIN32)
    localtime_s(&LocalTime, &Time);
#else
    localtime_r(&Time, &LocalTime);
#endif

    std::ostringstream Line;
    Line << "[" << std::put_time(&LocalTime, "%H:%M:%S") << "]";
    Line << "[" << Category << "][" << ToString(Level) << "] " << Message;

    const std::scoped_lock Lock(GLogMutex);
    FLogRecord Record;
    Record.Sequence = ++GLogSequence;
    Record.Level = Level;
    Record.Category = Category;
    Record.Message = Message;
    Record.FormattedLine = Line.str();
    GLogHistory.push_back(Record);
    if (GLogHistory.size() > MaxLogHistory)
    {
        GLogHistory.pop_front();
    }

    if (GLogFile.is_open())
    {
        GLogFile << Record.FormattedLine << '\n';
        GLogFile.flush();
    }
    if (GConsoleOutputEnabled)
    {
        std::ostream& Stream = Level == ELogLevel::Error ? std::cerr : std::cout;
        Stream << Record.FormattedLine << '\n';
    }
}

void FLog::SetConsoleOutputEnabled(bool bEnabled)
{
    const std::scoped_lock Lock(GLogMutex);
    GConsoleOutputEnabled = bEnabled;
}

bool FLog::IsConsoleOutputEnabled()
{
    const std::scoped_lock Lock(GLogMutex);
    return GConsoleOutputEnabled;
}

bool FLog::SetOutputFile(const std::filesystem::path& FilePath, bool bAppend)
{
    if (FilePath.empty())
    {
        return false;
    }
    std::error_code Error;
    if (!FilePath.parent_path().empty())
    {
        std::filesystem::create_directories(FilePath.parent_path(), Error);
        if (Error)
        {
            return false;
        }
    }

    std::ofstream NewFile(
        FilePath,
        std::ios::out | (bAppend ? std::ios::app : std::ios::trunc));
    if (!NewFile.is_open())
    {
        return false;
    }

    const std::scoped_lock Lock(GLogMutex);
    GLogFile = std::move(NewFile);
    for (const FLogRecord& Record : GLogHistory)
    {
        GLogFile << Record.FormattedLine << '\n';
    }
    GLogFile.flush();
    return GLogFile.good();
}

void FLog::CloseOutputFile()
{
    const std::scoped_lock Lock(GLogMutex);
    if (GLogFile.is_open())
    {
        GLogFile.flush();
        GLogFile.close();
    }
}

std::uint64_t FLog::GetLatestSequence()
{
    const std::scoped_lock Lock(GLogMutex);
    return GLogSequence;
}

std::vector<FLogRecord> FLog::GetRecordsSince(std::uint64_t Sequence)
{
    const std::scoped_lock Lock(GLogMutex);
    std::vector<FLogRecord> Result;
    for (const FLogRecord& Record : GLogHistory)
    {
        if (Record.Sequence > Sequence)
        {
            Result.push_back(Record);
        }
    }
    return Result;
}
}
