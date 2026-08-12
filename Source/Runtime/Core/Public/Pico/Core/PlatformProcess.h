#pragma once

#include "Pico/Core/Types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Pico
{
class FProcessHandle
{
public:
    FProcessHandle() = default;
    ~FProcessHandle();

    FProcessHandle(const FProcessHandle&) = delete;
    FProcessHandle& operator=(const FProcessHandle&) = delete;
    FProcessHandle(FProcessHandle&& Other) noexcept;
    FProcessHandle& operator=(FProcessHandle&& Other) noexcept;

    bool IsValid() const;
    uint32 GetProcessId() const;
    void Reset();

private:
    void* NativeHandle = nullptr;
    uint32 ProcessId = 0;

    friend class FPlatformProcess;
};

class FPlatformProcess
{
public:
    static FProcessHandle CreateProcess(
        const std::filesystem::path& Executable,
        const std::vector<std::string>& Arguments = {},
        const std::filesystem::path& WorkingDirectory = {},
        const std::filesystem::path& OutputFile = {},
        std::string* OutError = nullptr);
    static FProcessHandle CreateProcess(
        const std::filesystem::path& Executable,
        const std::vector<std::string>& Arguments,
        const std::filesystem::path& WorkingDirectory,
        std::string* OutError)
    {
        return CreateProcess(Executable, Arguments, WorkingDirectory, {}, OutError);
    }
    static bool IsRunning(const FProcessHandle& Process);
    static bool WaitForExit(
        const FProcessHandle& Process,
        uint32 TimeoutMilliseconds,
        int* OutExitCode = nullptr);
    static bool Terminate(FProcessHandle& Process, int ExitCode = 1);
};
}
