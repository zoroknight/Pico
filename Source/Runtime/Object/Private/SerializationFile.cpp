#include "Pico/Object/SerializationFile.h"

#include "Pico/Core/Log.h"

namespace Pico::Detail
{
bool ReplaceSerializedFile(
    const std::filesystem::path& TemporaryPath,
    const std::filesystem::path& FilePath,
    bool bKeepBackup)
{
    std::error_code ErrorCode;
    const bool bFileExists = std::filesystem::exists(FilePath, ErrorCode);
    if (ErrorCode)
    {
        PICO_LOG(LogObject, Error, "Could not inspect serialized file '{}': {}", FilePath.string(), ErrorCode.message());
        return false;
    }
    const bool bHasExistingFile = bFileExists
        && std::filesystem::is_regular_file(FilePath, ErrorCode);
    if (ErrorCode)
    {
        PICO_LOG(LogObject, Error, "Could not inspect serialized file type '{}': {}", FilePath.string(), ErrorCode.message());
        return false;
    }
    if (!bKeepBackup || !bHasExistingFile)
    {
        ErrorCode.clear();
        std::filesystem::rename(TemporaryPath, FilePath, ErrorCode);
        if (!ErrorCode)
        {
            return true;
        }
        PICO_LOG(LogObject, Error, "Direct serialized file replacement '{}' -> '{}' failed: {}", TemporaryPath.string(), FilePath.string(), ErrorCode.message());
    }

    ErrorCode.clear();
    if (!std::filesystem::is_regular_file(FilePath, ErrorCode))
    {
        PICO_LOG(LogObject, Error, "Serialized replacement target '{}' is unavailable: {}", FilePath.string(), ErrorCode.message());
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
        PICO_LOG(LogObject, Error, "Could not move serialized target '{}' to backup '{}': {}", FilePath.string(), BackupPath.string(), ErrorCode.message());
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

    if (!bKeepBackup)
    {
        std::filesystem::remove(BackupPath, ErrorCode);
    }
    return true;
}
}
