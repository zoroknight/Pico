#include "Pico/Core/PlatformProcess.h"

#include "Pico/Core/Platform.h"

#include <utility>

#if PICO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#ifdef CreateProcess
#undef CreateProcess
#endif

#include <algorithm>
#include <limits>
#endif

namespace Pico
{
namespace
{
void SetError(std::string* OutError, std::string Message)
{
    if (OutError != nullptr)
    {
        *OutError = std::move(Message);
    }
}

#if PICO_PLATFORM_WINDOWS
std::wstring Utf8ToWide(const std::string& Text)
{
    if (Text.empty())
    {
        return {};
    }
    const int Length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(),
        static_cast<int>(Text.size()), nullptr, 0);
    if (Length <= 0)
    {
        return {};
    }
    std::wstring Result(static_cast<std::size_t>(Length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(),
        static_cast<int>(Text.size()), Result.data(), Length);
    return Result;
}

void AppendQuotedArgument(std::wstring& CommandLine, const std::wstring& Argument)
{
    if (!CommandLine.empty())
    {
        CommandLine.push_back(L' ');
    }
    if (!Argument.empty()
        && Argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        CommandLine += Argument;
        return;
    }

    CommandLine.push_back(L'\"');
    std::size_t BackslashCount = 0;
    for (const wchar_t Character : Argument)
    {
        if (Character == L'\\')
        {
            ++BackslashCount;
            continue;
        }
        if (Character == L'\"')
        {
            CommandLine.append(BackslashCount * 2 + 1, L'\\');
            CommandLine.push_back(L'\"');
            BackslashCount = 0;
            continue;
        }
        CommandLine.append(BackslashCount, L'\\');
        BackslashCount = 0;
        CommandLine.push_back(Character);
    }
    CommandLine.append(BackslashCount * 2, L'\\');
    CommandLine.push_back(L'\"');
}

std::string GetWindowsErrorMessage(unsigned long ErrorCode)
{
    wchar_t* Buffer = nullptr;
    const DWORD Length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        ErrorCode,
        0,
        reinterpret_cast<wchar_t*>(&Buffer),
        0,
        nullptr);
    if (Length == 0 || Buffer == nullptr)
    {
        return "Windows error " + std::to_string(ErrorCode);
    }
    const std::wstring WideMessage(Buffer, Length);
    LocalFree(Buffer);

    const int Utf8Length = WideCharToMultiByte(
        CP_UTF8, 0, WideMessage.data(), static_cast<int>(WideMessage.size()),
        nullptr, 0, nullptr, nullptr);
    std::string Message(static_cast<std::size_t>(std::max(Utf8Length, 0)), '\0');
    if (Utf8Length > 0)
    {
        WideCharToMultiByte(
            CP_UTF8, 0, WideMessage.data(), static_cast<int>(WideMessage.size()),
            Message.data(), Utf8Length, nullptr, nullptr);
    }
    while (!Message.empty()
        && (Message.back() == '\r' || Message.back() == '\n'))
    {
        Message.pop_back();
    }
    return Message;
}
#endif
}

FProcessHandle::~FProcessHandle()
{
    Reset();
}

FProcessHandle::FProcessHandle(FProcessHandle&& Other) noexcept
    : NativeHandle(std::exchange(Other.NativeHandle, nullptr))
    , ProcessId(std::exchange(Other.ProcessId, 0))
{
}

FProcessHandle& FProcessHandle::operator=(FProcessHandle&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        NativeHandle = std::exchange(Other.NativeHandle, nullptr);
        ProcessId = std::exchange(Other.ProcessId, 0);
    }
    return *this;
}

bool FProcessHandle::IsValid() const
{
    return NativeHandle != nullptr;
}

uint32 FProcessHandle::GetProcessId() const
{
    return ProcessId;
}

void FProcessHandle::Reset()
{
#if PICO_PLATFORM_WINDOWS
    if (NativeHandle != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(NativeHandle));
    }
#endif
    NativeHandle = nullptr;
    ProcessId = 0;
}

FProcessHandle FPlatformProcess::CreateProcess(
    const std::filesystem::path& Executable,
    const std::vector<std::string>& Arguments,
    const std::filesystem::path& WorkingDirectory,
    std::string* OutError)
{
    if (OutError != nullptr)
    {
        OutError->clear();
    }
    FProcessHandle Result;
    if (Executable.empty() || !std::filesystem::is_regular_file(Executable))
    {
        SetError(OutError, "Executable does not exist");
        return Result;
    }

#if PICO_PLATFORM_WINDOWS
    const std::wstring ExecutablePath =
        std::filesystem::absolute(Executable).lexically_normal().wstring();
    std::wstring CommandLine;
    AppendQuotedArgument(CommandLine, ExecutablePath);
    for (const std::string& Argument : Arguments)
    {
        const std::wstring WideArgument = Utf8ToWide(Argument);
        if (!Argument.empty() && WideArgument.empty())
        {
            SetError(OutError, "A process argument is not valid UTF-8");
            return Result;
        }
        AppendQuotedArgument(CommandLine, WideArgument);
    }

    std::wstring WorkingDirectoryPath;
    if (!WorkingDirectory.empty())
    {
        WorkingDirectoryPath = std::filesystem::absolute(
            WorkingDirectory).lexically_normal().wstring();
        if (!std::filesystem::is_directory(WorkingDirectoryPath))
        {
            SetError(OutError, "Working directory does not exist");
            return Result;
        }
    }

    STARTUPINFOW StartupInfo {};
    StartupInfo.cb = sizeof(StartupInfo);
    PROCESS_INFORMATION ProcessInformation {};
    const BOOL bCreated = CreateProcessW(
        ExecutablePath.c_str(),
        CommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        WorkingDirectoryPath.empty() ? nullptr : WorkingDirectoryPath.c_str(),
        &StartupInfo,
        &ProcessInformation);
    if (!bCreated)
    {
        SetError(OutError, GetWindowsErrorMessage(GetLastError()));
        return Result;
    }

    CloseHandle(ProcessInformation.hThread);
    Result.NativeHandle = ProcessInformation.hProcess;
    Result.ProcessId = static_cast<uint32>(ProcessInformation.dwProcessId);
#else
    (void)Arguments;
    (void)WorkingDirectory;
    SetError(OutError, "Process creation is not implemented on this platform");
#endif
    return Result;
}

bool FPlatformProcess::IsRunning(const FProcessHandle& Process)
{
#if PICO_PLATFORM_WINDOWS
    if (!Process.IsValid())
    {
        return false;
    }
    DWORD ExitCode = 0;
    return GetExitCodeProcess(
        static_cast<HANDLE>(Process.NativeHandle), &ExitCode)
        && ExitCode == STILL_ACTIVE;
#else
    (void)Process;
    return false;
#endif
}

bool FPlatformProcess::WaitForExit(
    const FProcessHandle& Process,
    uint32 TimeoutMilliseconds,
    int* OutExitCode)
{
    if (OutExitCode != nullptr)
    {
        *OutExitCode = 0;
    }
#if PICO_PLATFORM_WINDOWS
    if (!Process.IsValid())
    {
        return false;
    }
    const DWORD WaitResult = WaitForSingleObject(
        static_cast<HANDLE>(Process.NativeHandle),
        TimeoutMilliseconds == std::numeric_limits<uint32>::max()
            ? INFINITE : static_cast<DWORD>(TimeoutMilliseconds));
    if (WaitResult != WAIT_OBJECT_0)
    {
        return false;
    }
    DWORD ExitCode = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(Process.NativeHandle), &ExitCode))
    {
        return false;
    }
    if (OutExitCode != nullptr)
    {
        *OutExitCode = static_cast<int>(ExitCode);
    }
    return true;
#else
    (void)Process;
    (void)TimeoutMilliseconds;
    return false;
#endif
}

bool FPlatformProcess::Terminate(FProcessHandle& Process, int ExitCode)
{
#if PICO_PLATFORM_WINDOWS
    return Process.IsValid()
        && TerminateProcess(
            static_cast<HANDLE>(Process.NativeHandle),
            static_cast<unsigned int>(ExitCode));
#else
    (void)Process;
    (void)ExitCode;
    return false;
#endif
}
}
