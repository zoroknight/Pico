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
#include <cwchar>
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

bool BuildEnvironmentBlock(
    const std::vector<std::pair<std::string, std::string>>& Overrides,
    std::vector<wchar_t>& OutBlock,
    std::string* OutError)
{
    std::vector<std::wstring> Entries;
    LPWCH Environment = GetEnvironmentStringsW();
    if (Environment == nullptr)
    {
        SetError(OutError, GetWindowsErrorMessage(GetLastError()));
        return false;
    }
    for (const wchar_t* Entry = Environment; *Entry != L'\0';)
    {
        const std::size_t Length = std::wcslen(Entry);
        Entries.emplace_back(Entry, Length);
        Entry += Length + 1;
    }
    FreeEnvironmentStringsW(Environment);

    for (const auto& [Name, Value] : Overrides)
    {
        if (Name.empty() || Name.find('=') != std::string::npos
            || Name.find('\0') != std::string::npos
            || Value.find('\0') != std::string::npos)
        {
            SetError(OutError, "A process environment override is invalid");
            return false;
        }
        const std::wstring WideName = Utf8ToWide(Name);
        const std::wstring WideValue = Utf8ToWide(Value);
        if (WideName.empty() || (!Value.empty() && WideValue.empty()))
        {
            SetError(OutError, "A process environment override is not valid UTF-8");
            return false;
        }
        const std::wstring Prefix = WideName + L"=";
        const auto Existing = std::find_if(
            Entries.begin(), Entries.end(),
            [&WideName](const std::wstring& Entry)
            {
                const std::size_t Separator = Entry.find(L'=');
                return Separator == WideName.size()
                    && _wcsnicmp(Entry.c_str(), WideName.c_str(), WideName.size()) == 0;
            });
        const std::wstring Replacement = Prefix + WideValue;
        if (Existing != Entries.end()) *Existing = Replacement;
        else Entries.push_back(Replacement);
    }
    std::sort(Entries.begin(), Entries.end(),
        [](const std::wstring& Left, const std::wstring& Right)
        {
            return _wcsicmp(Left.c_str(), Right.c_str()) < 0;
        });
    std::size_t CharacterCount = 1;
    for (const std::wstring& Entry : Entries)
        CharacterCount += Entry.size() + 1;
    OutBlock.clear();
    OutBlock.reserve(CharacterCount);
    for (const std::wstring& Entry : Entries)
    {
        OutBlock.insert(OutBlock.end(), Entry.begin(), Entry.end());
        OutBlock.push_back(L'\0');
    }
    OutBlock.push_back(L'\0');
    return true;
}
#endif
}

FProcessGroup::~FProcessGroup()
{
    Reset();
}

FProcessGroup::FProcessGroup(FProcessGroup&& Other) noexcept
    : NativeHandle(std::exchange(Other.NativeHandle, nullptr))
{
}

FProcessGroup& FProcessGroup::operator=(FProcessGroup&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        NativeHandle = std::exchange(Other.NativeHandle, nullptr);
    }
    return *this;
}

bool FProcessGroup::InitializeKillOnClose(std::string* OutError)
{
    if (OutError) OutError->clear();
    Reset();
#if PICO_PLATFORM_WINDOWS
    HANDLE Job = CreateJobObjectW(nullptr, nullptr);
    if (Job == nullptr)
    {
        SetError(OutError, GetWindowsErrorMessage(GetLastError()));
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits {};
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
    {
        SetError(OutError, GetWindowsErrorMessage(GetLastError()));
        CloseHandle(Job);
        return false;
    }
    NativeHandle = Job;
    return true;
#else
    SetError(OutError, "Process groups are not implemented on this platform");
    return false;
#endif
}

bool FProcessGroup::IsValid() const
{
    return NativeHandle != nullptr;
}

void FProcessGroup::Reset()
{
#if PICO_PLATFORM_WINDOWS
    if (NativeHandle != nullptr) CloseHandle(static_cast<HANDLE>(NativeHandle));
#endif
    NativeHandle = nullptr;
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
    const std::filesystem::path& OutputFile,
    std::string* OutError,
    FProcessGroup* ProcessGroup,
    const FProcessLaunchOptions& LaunchOptions)
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
    if (ProcessGroup != nullptr && !ProcessGroup->IsValid())
    {
        SetError(OutError, "Process group is not initialized");
        return Result;
    }
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
    HANDLE OutputHandle = INVALID_HANDLE_VALUE;
    if (!OutputFile.empty())
    {
        std::error_code DirectoryError;
        std::filesystem::create_directories(OutputFile.parent_path(), DirectoryError);
        if (DirectoryError)
        {
            SetError(OutError, "Could not create process log directory");
            return Result;
        }
        OutputHandle = CreateFileW(
            std::filesystem::absolute(OutputFile).lexically_normal().wstring().c_str(),
            GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (OutputHandle == INVALID_HANDLE_VALUE)
        {
            SetError(OutError, GetWindowsErrorMessage(GetLastError()));
            return Result;
        }
        SetHandleInformation(OutputHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        StartupInfo.hStdOutput = OutputHandle;
        StartupInfo.hStdError = OutputHandle;
        StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION ProcessInformation {};
    std::vector<wchar_t> EnvironmentBlock;
    if (!LaunchOptions.EnvironmentOverrides.empty()
        && !BuildEnvironmentBlock(
            LaunchOptions.EnvironmentOverrides, EnvironmentBlock, OutError))
    {
        if (OutputHandle != INVALID_HANDLE_VALUE) CloseHandle(OutputHandle);
        return Result;
    }
    const DWORD CreationFlags = CREATE_NEW_PROCESS_GROUP
        | (ProcessGroup != nullptr ? CREATE_SUSPENDED : 0)
        | (LaunchOptions.bCreateNewConsole ? CREATE_NEW_CONSOLE : 0)
        | (!EnvironmentBlock.empty() ? CREATE_UNICODE_ENVIRONMENT : 0);
    const BOOL bCreated = CreateProcessW(
        ExecutablePath.c_str(),
        CommandLine.data(),
        nullptr,
        nullptr,
        OutputHandle != INVALID_HANDLE_VALUE,
        CreationFlags,
        EnvironmentBlock.empty() ? nullptr : EnvironmentBlock.data(),
        WorkingDirectoryPath.empty() ? nullptr : WorkingDirectoryPath.c_str(),
        &StartupInfo,
        &ProcessInformation);
    if (OutputHandle != INVALID_HANDLE_VALUE) CloseHandle(OutputHandle);
    if (!bCreated)
    {
        SetError(OutError, GetWindowsErrorMessage(GetLastError()));
        return Result;
    }

    if (ProcessGroup != nullptr
        && !AssignProcessToJobObject(
            static_cast<HANDLE>(ProcessGroup->NativeHandle),
            ProcessInformation.hProcess))
    {
        const DWORD ErrorCode = GetLastError();
        TerminateProcess(ProcessInformation.hProcess, 1);
        CloseHandle(ProcessInformation.hThread);
        CloseHandle(ProcessInformation.hProcess);
        SetError(OutError, "Could not assign child process to its owner group: "
            + GetWindowsErrorMessage(ErrorCode));
        return Result;
    }
    if (ProcessGroup != nullptr && ResumeThread(ProcessInformation.hThread) == static_cast<DWORD>(-1))
    {
        const DWORD ErrorCode = GetLastError();
        TerminateProcess(ProcessInformation.hProcess, 1);
        CloseHandle(ProcessInformation.hThread);
        CloseHandle(ProcessInformation.hProcess);
        SetError(OutError, "Could not resume managed child process: "
            + GetWindowsErrorMessage(ErrorCode));
        return Result;
    }

    CloseHandle(ProcessInformation.hThread);
    Result.NativeHandle = ProcessInformation.hProcess;
    Result.ProcessId = static_cast<uint32>(ProcessInformation.dwProcessId);
#else
    (void)Arguments;
    (void)WorkingDirectory;
    (void)OutputFile;
    (void)ProcessGroup;
    (void)LaunchOptions;
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
