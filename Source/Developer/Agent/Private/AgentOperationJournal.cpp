#include "Pico/Agent/AgentOperationJournal.h"

#include "Pico/Core/Platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#if PICO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::int64_t NowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string CanonicalArguments(std::string_view Arguments)
{
    try
    {
        return FJson::parse(Arguments).dump();
    }
    catch (...)
    {
        return std::string(Arguments);
    }
}

std::string StableFileName(std::string_view Text)
{
    std::uint64_t Hash = 14695981039346656037ull;
    for (const unsigned char Character : Text)
    {
        Hash ^= Character;
        Hash *= 1099511628211ull;
    }
    std::ostringstream Stream;
    Stream << std::hex << std::setfill('0') << std::setw(16) << Hash;
    return Stream.str() + ".json";
}

bool ParseState(std::string_view Text, EAgentOperationState& OutState)
{
    if (Text == "Prepared") OutState = EAgentOperationState::Prepared;
    else if (Text == "Executing") OutState = EAgentOperationState::Executing;
    else if (Text == "Applied") OutState = EAgentOperationState::Applied;
    else if (Text == "Committed") OutState = EAgentOperationState::Committed;
    else return false;
    return true;
}

bool AtomicWrite(
    const std::filesystem::path& Path,
    std::string_view Contents,
    std::string* OutError)
{
    std::error_code Error;
    std::filesystem::create_directories(Path.parent_path(), Error);
    if (Error)
    {
        if (OutError) *OutError = "Could not create operation journal directory: "
            + Error.message();
        return false;
    }
    const std::filesystem::path Temporary = Path.string() + ".tmp";
    {
        std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
        Stream.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
        Stream.flush();
        if (!Stream)
        {
            if (OutError) *OutError = "Could not flush operation journal record";
            return false;
        }
    }
#if PICO_PLATFORM_WINDOWS
    if (MoveFileExW(Temporary.wstring().c_str(), Path.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return true;
    }
    if (OutError) *OutError = "Could not commit operation journal record: Windows error "
        + std::to_string(GetLastError());
#else
    std::filesystem::rename(Temporary, Path, Error);
    if (!Error) return true;
    if (OutError) *OutError = "Could not commit operation journal record: "
        + Error.message();
#endif
    return false;
}

std::optional<FAgentOperationRecord> ReadRecord(
    const std::filesystem::path& Path,
    std::string* OutError)
{
    try
    {
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream) return std::nullopt;
        const FJson Json = FJson::parse(Stream);
        if (Json.at("version").get<int>() != 1)
            throw std::runtime_error("unsupported record version");
        FAgentOperationRecord Record;
        Record.OperationId = Json.at("operation_id").get<std::string>();
        Record.ToolName = Json.at("tool_name").get<std::string>();
        Record.ArgumentsJson = Json.at("arguments").dump();
        if (!ParseState(Json.at("state").get<std::string>(), Record.State))
            throw std::runtime_error("unknown operation state");
        Record.UpdatedAtMilliseconds = Json.value("updated_at_ms", 0ll);
        if (Json.contains("result"))
        {
            const FJson& ResultJson = Json.at("result");
            FAgentToolResult Result;
            const bool bHasStructured = ResultJson.contains("structured_result")
                && DeserializeAgentToolResult(
                    ResultJson.at("structured_result").dump(), Result);
            if (!bHasStructured)
            {
                Result.CallId = Record.OperationId;
                Result.bSucceeded = ResultJson.value("succeeded", false);
                Result.OutputJson = ResultJson.value("output", FJson::object()).dump();
                Result.Error = ResultJson.value("error", "");
                Result.bReused = ResultJson.value("reused", false);
                NormalizeAgentToolResult(Result);
            }
            Record.Result = std::move(Result);
        }
        return Record;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = "Could not read operation journal record: "
            + std::string(Exception.what());
        return std::nullopt;
    }
}
}

FAgentOperationJournal::FAgentOperationJournal(std::filesystem::path InDirectory)
    : Directory(std::move(InDirectory))
{
}

bool FAgentOperationJournal::Prepare(
    const FAgentToolCall& Call,
    std::string* OutError)
{
    std::lock_guard Lock(Mutex);
    if (const auto Existing = Load(Call, OutError))
    {
        if (Existing->ToolName != Call.Name
            || Existing->ArgumentsJson != CanonicalArguments(Call.ArgumentsJson))
        {
            if (OutError) *OutError = "Operation id was reused with different input";
            return false;
        }
        return true;
    }
    FAgentOperationRecord Record;
    Record.OperationId = Call.Id;
    Record.ToolName = Call.Name;
    Record.ArgumentsJson = CanonicalArguments(Call.ArgumentsJson);
    Record.State = EAgentOperationState::Prepared;
    Record.UpdatedAtMilliseconds = NowMilliseconds();
    return Save(Record, OutError);
}

bool FAgentOperationJournal::MarkExecuting(
    const FAgentToolCall& Call,
    std::string* OutError)
{
    std::lock_guard Lock(Mutex);
    auto Record = Load(Call, OutError);
    if (!Record)
    {
        if (OutError && OutError->empty()) *OutError = "Prepared operation was not found";
        return false;
    }
    Record->State = EAgentOperationState::Executing;
    Record->UpdatedAtMilliseconds = NowMilliseconds();
    return Save(*Record, OutError);
}

bool FAgentOperationJournal::MarkApplied(
    const FAgentToolCall& Call,
    const FAgentToolResult& Result,
    std::string* OutError)
{
    std::lock_guard Lock(Mutex);
    auto Record = Load(Call, OutError);
    if (!Record)
    {
        if (OutError && OutError->empty()) *OutError = "Executing operation was not found";
        return false;
    }
    Record->State = EAgentOperationState::Applied;
    Record->Result = Result;
    Record->Result->CallId = Call.Id;
    Record->UpdatedAtMilliseconds = NowMilliseconds();
    return Save(*Record, OutError);
}

bool FAgentOperationJournal::MarkCommitted(
    const FAgentToolCall& Call,
    std::string* OutError)
{
    std::lock_guard Lock(Mutex);
    auto Record = Load(Call, OutError);
    if (!Record)
    {
        if (OutError && OutError->empty()) *OutError = "Applied operation was not found";
        return false;
    }
    Record->State = EAgentOperationState::Committed;
    Record->UpdatedAtMilliseconds = NowMilliseconds();
    return Save(*Record, OutError);
}

std::optional<FAgentToolResult> FAgentOperationJournal::FindApplied(
    const FAgentToolCall& Call,
    std::string* OutError) const
{
    std::lock_guard Lock(Mutex);
    const auto Record = Load(Call, OutError);
    if (!Record || Record->ToolName != Call.Name
        || Record->ArgumentsJson != CanonicalArguments(Call.ArgumentsJson)
        || (Record->State != EAgentOperationState::Applied
            && Record->State != EAgentOperationState::Committed)
        || !Record->Result)
    {
        return std::nullopt;
    }
    FAgentToolResult Result = *Record->Result;
    Result.bReused = true;
    return Result;
}

std::vector<FAgentOperationRecord> FAgentOperationJournal::ListIncomplete() const
{
    std::lock_guard Lock(Mutex);
    if (bIncompleteLoaded) return IncompleteCache;
    std::error_code Error;
    if (!std::filesystem::is_directory(Directory, Error))
    {
        if (!Error) bIncompleteLoaded = true;
        return {};
    }
    for (const auto& Entry : std::filesystem::directory_iterator(Directory, Error))
    {
        if (Error || !Entry.is_regular_file() || Entry.path().extension() != ".json")
            continue;
        if (auto Record = ReadRecord(Entry.path(), nullptr);
            Record && Record->State != EAgentOperationState::Committed)
        {
            IncompleteCache.push_back(std::move(*Record));
        }
    }
    if (Error)
    {
        IncompleteCache.clear();
        return {};
    }
    bIncompleteLoaded = true;
    return IncompleteCache;
}

std::filesystem::path FAgentOperationJournal::RecordPath(
    std::string_view OperationId) const
{
    return Directory / StableFileName(OperationId);
}

std::optional<FAgentOperationRecord> FAgentOperationJournal::Load(
    const FAgentToolCall& Call,
    std::string* OutError) const
{
    return ReadRecord(RecordPath(Call.Id), OutError);
}

bool FAgentOperationJournal::Save(
    const FAgentOperationRecord& Record,
    std::string* OutError) const
{
    FJson Json { {"version", 1}, {"operation_id", Record.OperationId},
        {"tool_name", Record.ToolName},
        {"arguments", FJson::parse(Record.ArgumentsJson)},
        {"state", ToString(Record.State)},
        {"updated_at_ms", Record.UpdatedAtMilliseconds} };
    if (Record.Result)
    {
        Json["result"] = {{"succeeded", Record.Result->bSucceeded},
            {"output", FJson::parse(Record.Result->OutputJson)},
            {"error", Record.Result->Error}, {"reused", Record.Result->bReused},
            {"structured_result", FJson::parse(
                SerializeAgentToolResult(*Record.Result))}};
    }
    if (!AtomicWrite(RecordPath(Record.OperationId), Json.dump(2), OutError))
        return false;
    if (bIncompleteLoaded)
    {
        std::erase_if(IncompleteCache, [&Record](const FAgentOperationRecord& Item)
        {
            return Item.OperationId == Record.OperationId;
        });
        if (Record.State != EAgentOperationState::Committed)
            IncompleteCache.push_back(Record);
    }
    return true;
}

std::string_view ToString(EAgentOperationState State)
{
    switch (State)
    {
    case EAgentOperationState::Prepared: return "Prepared";
    case EAgentOperationState::Executing: return "Executing";
    case EAgentOperationState::Applied: return "Applied";
    case EAgentOperationState::Committed: return "Committed";
    }
    return "Prepared";
}
}
