#include "Pico/Object/SerializationFile.h"

#include "Pico/Core/Log.h"

namespace Pico::Detail
{
bool ReplaceSerializedFile(
    const std::filesystem::path& TemporaryPath,
    const std::filesystem::path& FilePath)
{
    std::error_code ErrorCode;
    std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
    if (!ErrorCode)
    {
        return true;
    }

    ErrorCode.clear();
    if (!std::filesystem::is_regular_file(FilePath, ErrorCode))
    {
        return false;
    }

    std::filesystem::path BackupPath = FilePath;
    BackupPath += ".bak";
    std::filesystem::remove(BackupPath, ErrorCode);
    if (ErrorCode)
    {
        PICO_LOG(
            LogObject,
            Error,
            "Could not remove stale serialization backup '{}': {}",
            BackupPath.string(),
            ErrorCode.message());
        return false;
    }

    ErrorCode.clear();
    std::filesystem::rename(FilePath, BackupPath, ErrorCode);
    if (ErrorCode)
    {
        return false;
    }

    ErrorCode.clear();
    std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
    if (ErrorCode)
    {
        std::error_code RestoreError;
        std::filesystem::rename(BackupPath, FilePath, RestoreError);
        if (RestoreError)
        {
            PICO_LOG(
                LogObject,
                Error,
                "Could not restore '{}' from backup '{}': {}. Temporary file remains at '{}'",
                FilePath.string(),
                BackupPath.string(),
                RestoreError.message(),
                TemporaryPath.string());
        }
        return false;
    }

    std::filesystem::remove(BackupPath, ErrorCode);
    return true;
}
}
