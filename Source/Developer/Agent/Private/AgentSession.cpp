#include "Pico/Agent/AgentSession.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <unordered_set>

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

FJson ToJson(const FAgentEvent& Event, std::string_view SessionId)
{
    return {
        {"version", 1}, {"session_id", SessionId}, {"sequence", Event.Sequence},
        {"timestamp_ms", Event.TimestampMilliseconds}, {"type", ToString(Event.Type)},
        {"status", ToString(Event.Status)}, {"role", ToString(Event.Role)},
        {"content", Event.Content}, {"call_id", Event.CallId}, {"tool_name", Event.ToolName},
        {"payload", FJson::parse(Event.PayloadJson)}, {"succeeded", Event.bSucceeded},
        {"trace", FJson::parse(Event.TraceJson)}, {"reused", Event.bReused},
        {"run_id", Event.RunId}, {"turn_id", Event.TurnId},
        {"span_id", Event.SpanId}, {"parent_span_id", Event.ParentSpanId},
        {"span_name", Event.SpanName},
        {"started_timestamp_ms", Event.StartedTimestampMilliseconds},
        {"duration_us", Event.DurationMicroseconds},
        {"steps", Event.Counters.Steps},
        {"tool_calls", Event.Counters.ToolCalls},
        {"read_only_tool_calls", Event.Counters.ReadOnlyToolCalls},
        {"mutation_tool_calls", Event.Counters.MutationToolCalls},
        {"semantic_cache_hits", Event.Counters.SemanticCacheHits},
        {"consecutive_no_progress_steps", Event.Counters.ConsecutiveNoProgressSteps},
        {"repair_attempts", Event.Counters.RepairAttempts}
    };
}

bool FromJson(const FJson& Json, std::string_view SessionId, FAgentEvent& Out, std::string& Error)
{
    try
    {
        if (Json.at("version").get<int>() != 1
            || Json.at("session_id").get<std::string>() != SessionId)
        {
            Error = "Session event version or id does not match";
            return false;
        }
        if (!TryParseAgentEventType(Json.at("type").get<std::string>(), Out.Type)
            || !TryParseAgentStatus(Json.at("status").get<std::string>(), Out.Status)
            || !TryParseAgentRole(Json.at("role").get<std::string>(), Out.Role))
        {
            Error = "Session event contains an unknown enum value";
            return false;
        }
        Out.Sequence = Json.at("sequence").get<std::uint64_t>();
        Out.TimestampMilliseconds = Json.at("timestamp_ms").get<std::int64_t>();
        Out.Content = Json.value("content", "");
        Out.CallId = Json.value("call_id", "");
        Out.ToolName = Json.value("tool_name", "");
        Out.PayloadJson = Json.value("payload", FJson::object()).dump();
        Out.TraceJson = Json.value("trace", FJson::array()).dump();
        Out.RunId = Json.value("run_id", "");
        Out.TurnId = Json.value("turn_id", "");
        Out.SpanId = Json.value("span_id", "");
        Out.ParentSpanId = Json.value("parent_span_id", "");
        Out.SpanName = Json.value("span_name", "");
        Out.StartedTimestampMilliseconds = Json.value(
            "started_timestamp_ms", std::int64_t {0});
        Out.DurationMicroseconds = Json.value("duration_us", std::uint64_t {0});
        Out.bSucceeded = Json.value("succeeded", false);
        Out.bReused = Json.value("reused", false);
        Out.Counters.Steps = Json.value("steps", 0U);
        Out.Counters.ToolCalls = Json.value("tool_calls", 0U);
        Out.Counters.ReadOnlyToolCalls = Json.value("read_only_tool_calls", 0U);
        Out.Counters.MutationToolCalls = Json.value("mutation_tool_calls", 0U);
        Out.Counters.SemanticCacheHits = Json.value("semantic_cache_hits", 0U);
        Out.Counters.ConsecutiveNoProgressSteps =
            Json.value("consecutive_no_progress_steps", 0U);
        Out.Counters.RepairAttempts = Json.value("repair_attempts", 0U);
        return true;
    }
    catch (const std::exception& Exception)
    {
        Error = Exception.what();
        return false;
    }
}
}

std::optional<FAgentSession> FAgentSession::OpenOrCreate(
    std::string InSessionId,
    const std::filesystem::path& InEventLogPath,
    std::string* OutError)
{
    if (InSessionId.empty() || InEventLogPath.empty())
    {
        if (OutError) *OutError = "Session id and event log path are required";
        return std::nullopt;
    }
    FAgentSession Session;
    Session.SessionId = std::move(InSessionId);
    Session.EventLogPath = InEventLogPath;
    if (!Session.Load(OutError)) return std::nullopt;
    if (Session.Events.empty())
    {
        FAgentEvent Created;
        Created.Type = EAgentEventType::SessionCreated;
        Created.Content = "Pico Agent session created";
        if (!Session.Append(std::move(Created), OutError)) return std::nullopt;
    }
    return Session;
}

bool FAgentSession::Append(FAgentEvent Event, std::string* OutError)
{
    try
    {
        if (Event.RunId.empty()) Event.RunId = CurrentRunId;
        if (Event.TurnId.empty()) Event.TurnId = CurrentTurnId;
        if (Event.SpanId.empty()) Event.SpanId = CurrentSpanId;
        const FJson ValidatedPayload = FJson::parse(Event.PayloadJson);
        Event.PayloadJson = ValidatedPayload.dump();
        const FJson ValidatedTrace = FJson::parse(Event.TraceJson);
        Event.TraceJson = ValidatedTrace.dump();
        std::filesystem::create_directories(EventLogPath.parent_path());
        Event.Sequence = Events.empty() ? 1 : Events.back().Sequence + 1;
        Event.TimestampMilliseconds = NowMilliseconds();
        std::ofstream Stream(EventLogPath, std::ios::app | std::ios::binary);
        if (!Stream)
        {
            if (OutError) *OutError = "Could not open session event log for append";
            return false;
        }
        Stream << ToJson(Event, SessionId).dump() << '\n';
        Stream.flush();
        if (!Stream)
        {
            if (OutError) *OutError = "Could not flush session event log";
            return false;
        }
        Events.push_back(Event);
        Apply(Events.back());
        return true;
    }
    catch (const std::exception& Exception)
    {
        if (OutError) *OutError = Exception.what();
        return false;
    }
}

void FAgentSession::SetTraceContext(
    std::string RunId,
    std::string TurnId,
    std::string SpanId)
{
    CurrentRunId = std::move(RunId);
    CurrentTurnId = std::move(TurnId);
    CurrentSpanId = std::move(SpanId);
}

bool FAgentSession::SetStatus(EAgentStatus InStatus, std::string* OutError)
{
    FAgentEvent Event;
    Event.Type = EAgentEventType::StatusChanged;
    Event.Status = InStatus;
    return Append(std::move(Event), OutError);
}

bool FAgentSession::WriteCheckpoint(
    EAgentStatus InStatus,
    const FAgentCounters& InCounters,
    std::string* OutError)
{
    FAgentEvent Event;
    Event.Type = EAgentEventType::Checkpoint;
    Event.Status = InStatus;
    Event.Counters = InCounters;
    return Append(std::move(Event), OutError);
}

const std::string& FAgentSession::GetId() const { return SessionId; }
const std::filesystem::path& FAgentSession::GetEventLogPath() const { return EventLogPath; }
const std::vector<FAgentEvent>& FAgentSession::GetEvents() const { return Events; }
EAgentStatus FAgentSession::GetStatus() const { return Status; }
const FAgentCounters& FAgentSession::GetCounters() const { return Counters; }

std::vector<FAgentMessage> FAgentSession::BuildMessageHistory() const
{
    std::vector<FAgentMessage> Result;
    std::unordered_set<std::string> IncludedToolResults;
    for (const FAgentEvent& Event : Events)
    {
        if (Event.Type == EAgentEventType::Message)
        {
            Result.push_back({Event.Role, Event.Content});
        }
        else if (Event.Type == EAgentEventType::ToolCall)
        {
            if (Result.empty() || Result.back().Role != EAgentRole::Assistant)
            {
                Result.push_back({EAgentRole::Assistant, {}});
            }
            Result.back().ToolCalls.push_back(
                {Event.CallId, Event.ToolName, Event.PayloadJson});
        }
        else if (Event.Type == EAgentEventType::ToolResult)
        {
            if (!IncludedToolResults.insert(Event.CallId).second)
                continue;
            Result.push_back({EAgentRole::Tool,
                Event.bSucceeded ? Event.PayloadJson : Event.Content,
                Event.CallId});
        }
    }
    return Result;
}

std::optional<FAgentToolResult> FAgentSession::FindToolResult(std::string_view CallId) const
{
    for (auto It = Events.rbegin(); It != Events.rend(); ++It)
    {
        if (It->Type == EAgentEventType::ToolResult && It->CallId == CallId)
        {
            return FAgentToolResult {It->CallId, It->bSucceeded, It->PayloadJson,
                It->bSucceeded ? std::string {} : It->Content, true};
        }
    }
    return std::nullopt;
}

std::optional<FAgentToolCall> FAgentSession::FindToolCall(std::string_view CallId) const
{
    for (auto It = Events.rbegin(); It != Events.rend(); ++It)
    {
        if (It->Type == EAgentEventType::ToolCall && It->CallId == CallId)
        {
            return FAgentToolCall {It->CallId, It->ToolName, It->PayloadJson};
        }
    }
    return std::nullopt;
}

bool FAgentSession::MatchesToolCall(const FAgentToolCall& Call) const
{
    const std::optional<FAgentToolCall> Existing = FindToolCall(Call.Id);
    if (!Existing || Existing->Name != Call.Name) return false;
    try
    {
        return FJson::parse(Existing->ArgumentsJson) == FJson::parse(Call.ArgumentsJson);
    }
    catch (...)
    {
        return Existing->ArgumentsJson == Call.ArgumentsJson;
    }
}

bool FAgentSession::Load(std::string* OutError)
{
    if (!std::filesystem::exists(EventLogPath)) return true;
    std::ifstream Stream(EventLogPath, std::ios::binary);
    std::vector<std::string> Lines;
    for (std::string Line; std::getline(Stream, Line);)
    {
        if (!Line.empty()) Lines.push_back(std::move(Line));
    }
    std::uint64_t ExpectedSequence = 1;
    bool bHadIncompleteTail = false;
    for (std::size_t Index = 0; Index < Lines.size(); ++Index)
    {
        FAgentEvent Event;
        std::string Error;
        try
        {
            if (!FromJson(FJson::parse(Lines[Index]), SessionId, Event, Error))
            {
                if (OutError) *OutError = Error;
                return false;
            }
        }
        catch (const std::exception& Exception)
        {
            // A crash can interrupt only the final append. Earlier corruption is fatal.
            if (Index + 1 == Lines.size())
            {
                bHadIncompleteTail = true;
                break;
            }
            if (OutError) *OutError = Exception.what();
            return false;
        }
        if (Event.Sequence != ExpectedSequence++)
        {
            if (OutError) *OutError = "Session event sequence is not contiguous";
            return false;
        }
        Events.push_back(Event);
        Apply(Events.back());
    }
    if (bHadIncompleteTail)
    {
        std::ofstream Repaired(EventLogPath, std::ios::binary | std::ios::trunc);
        if (!Repaired)
        {
            if (OutError) *OutError = "Could not repair incomplete session event tail";
            return false;
        }
        for (const FAgentEvent& Event : Events)
        {
            Repaired << ToJson(Event, SessionId).dump() << '\n';
        }
        Repaired.flush();
        if (!Repaired)
        {
            if (OutError) *OutError = "Could not flush repaired session event log";
            return false;
        }
    }
    return true;
}

void FAgentSession::Apply(const FAgentEvent& Event)
{
    if (Event.Type == EAgentEventType::StatusChanged
        || Event.Type == EAgentEventType::Checkpoint)
    {
        Status = Event.Status;
    }
    if (Event.Type == EAgentEventType::Checkpoint)
    {
        Counters = Event.Counters;
    }
}
}
