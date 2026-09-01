#include "Pico/Mcp/McpServerCore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Pico
{
using FJson = nlohmann::json;

struct FMcpCancellationToken::FState
{
    std::atomic<bool> bCancelled {false};
    std::chrono::steady_clock::time_point Deadline =
        std::chrono::steady_clock::time_point::max();
};

FMcpCancellationToken::FMcpCancellationToken() = default;

FMcpCancellationToken::FMcpCancellationToken(std::shared_ptr<FState> InState)
    : State(std::move(InState))
{
}

bool FMcpCancellationToken::IsCancellationRequested() const
{
    return State && State->bCancelled.load(std::memory_order_relaxed);
}

bool FMcpCancellationToken::IsExpired() const
{
    return State && std::chrono::steady_clock::now() >= State->Deadline;
}

namespace
{
constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;
constexpr int UnsupportedProtocolVersion = -32022;

enum class ELegacyState
{
    InitializeResponded,
    Ready
};

FJson ServerMeta(const FMcpServerInfo& Info)
{
    return {{"io.modelcontextprotocol/serverInfo",
        {{"name", Info.Name}, {"version", Info.Version}}}};
}

FMcpMessageResult Response(const FJson& Id, FJson Result)
{
    return {true, FJson {
        {"jsonrpc", "2.0"}, {"id", Id}, {"result", std::move(Result)}}.dump()};
}

FMcpMessageResult Error(
    const FJson& Id,
    int Code,
    std::string Message,
    FJson Data = FJson())
{
    FJson ErrorObject {{"code", Code}, {"message", std::move(Message)}};
    if (!Data.is_null())
        ErrorObject["data"] = std::move(Data);
    return {true, FJson {
        {"jsonrpc", "2.0"}, {"id", Id}, {"error", std::move(ErrorObject)}}.dump()};
}

bool IsValidId(const FJson& Id)
{
    return Id.is_string() || Id.is_number_integer() || Id.is_number_unsigned();
}

bool IsWithinJsonLimits(
    const FJson& Value,
    const FMcpLimits& Limits,
    std::size_t Depth = 1)
{
    if (Depth > Limits.MaxJsonDepth)
        return false;
    if (Value.is_string()
        && Value.get_ref<const std::string&>().size() > Limits.MaxStringBytes)
    {
        return false;
    }
    if (Value.is_object())
    {
        for (auto It = Value.begin(); It != Value.end(); ++It)
        {
            if (!IsWithinJsonLimits(It.value(), Limits, Depth + 1))
            {
                return false;
            }
        }
    }
    else if (Value.is_array())
    {
        for (const FJson& Entry : Value)
        {
            if (!IsWithinJsonLimits(Entry, Limits, Depth + 1))
                return false;
        }
    }
    return true;
}

bool TryReadModernVersion(const FJson& Request, std::string& OutVersion)
{
    if (!Request.contains("params") || !Request["params"].is_object())
        return false;
    const FJson& Params = Request["params"];
    if (!Params.contains("_meta") || !Params["_meta"].is_object())
        return false;
    const FJson& Meta = Params["_meta"];
    constexpr std::string_view Key =
        "io.modelcontextprotocol/protocolVersion";
    if (!Meta.contains(Key) || !Meta[Key].is_string())
        return false;
    OutVersion = Meta[Key].get<std::string>();
    return true;
}

bool HasModernCapabilities(const FJson& Request)
{
    const FJson& Meta = Request["params"]["_meta"];
    constexpr std::string_view Key =
        "io.modelcontextprotocol/clientCapabilities";
    return Meta.contains(Key) && Meta[Key].is_object();
}

bool SupportsModernFormElicitation(const FJson& Request)
{
    const FJson& Capabilities = Request["params"]["_meta"]
        ["io.modelcontextprotocol/clientCapabilities"];
    if (!Capabilities.contains("elicitation")
        || !Capabilities["elicitation"].is_object())
    {
        return false;
    }
    const FJson& Elicitation = Capabilities["elicitation"];
    return Elicitation.empty()
        || (Elicitation.contains("form") && Elicitation["form"].is_object());
}

FJson ParseSchema(const std::string& Text)
{
    FJson Schema = FJson::parse(Text, nullptr, false);
    if (Schema.is_discarded() || !Schema.is_object())
        throw std::runtime_error("Tool schema is not a JSON object");
    return Schema;
}
}

class FMcpServerCore::FImpl
{
public:
    FImpl(
        IMcpToolProvider& InToolProvider,
        FMcpServerInfo InServerInfo,
        FMcpLimits InLimits)
        : ToolProvider(InToolProvider)
        , ServerInfo(std::move(InServerInfo))
        , Limits(InLimits)
    {
    }

    FMcpMessageResult Handle(
        const FMcpRequestContext& Context,
        std::string_view MessageJson)
    {
        if (MessageJson.size() > Limits.MaxRequestBytes)
            return Error(nullptr, InvalidRequest, "Request exceeds size limit");

        FJson Request = FJson::parse(MessageJson, nullptr, false);
        if (Request.is_discarded())
            return Error(nullptr, ParseError, "Parse error");
        if (Request.is_array())
            return Error(nullptr, InvalidRequest, "JSON-RPC batches are not supported");
        if (!Request.is_object() || !IsWithinJsonLimits(Request, Limits))
            return Error(nullptr, InvalidRequest, "Invalid or over-limit request");
        if (!Request.contains("jsonrpc") || !Request["jsonrpc"].is_string()
            || Request["jsonrpc"].get<std::string>() != "2.0"
            || !Request.contains("method") || !Request["method"].is_string())
        {
            return Error(Request.value("id", FJson(nullptr)),
                InvalidRequest, "Invalid JSON-RPC request");
        }

        const bool bNotification = !Request.contains("id");
        if (!bNotification && !IsValidId(Request["id"]))
            return Error(nullptr, InvalidRequest, "Request id must be a string or integer");
        const FJson Id = bNotification ? FJson(nullptr) : Request["id"];
        const std::string Method = Request["method"].get<std::string>();

        if (Method == "notifications/cancelled")
        {
            HandleCancellation(Context, Request);
            return {};
        }
        if (Method == "notifications/initialized")
        {
            HandleLegacyInitialized(Context);
            return {};
        }
        if (bNotification)
            return {};

        std::string ModernVersion;
        const bool bModern = TryReadModernVersion(Request, ModernVersion);
        if (bModern)
        {
            if (!HasModernCapabilities(Request))
                return Error(Id, InvalidParams,
                    "Missing required modern MCP client capabilities");
            if (ModernVersion != McpProtocolModern)
            {
                return Error(Id, UnsupportedProtocolVersion,
                    "Unsupported protocol version",
                    {{"supported", {McpProtocolModern, McpProtocolLegacy}},
                        {"requested", ModernVersion}});
            }
            return HandleReadyRequest(Context, Request, true);
        }

        if (Method == "initialize")
            return HandleLegacyInitialize(Context, Request);
        if (Method == "ping")
            return Response(Id, FJson::object());
        if (!IsLegacyReady(Context.PeerId))
            return Error(Id, InvalidRequest,
                "MCP peer is not initialized; use modern request metadata or initialize");
        return HandleReadyRequest(Context, Request, false);
    }

    void ClosePeer(std::string_view PeerId)
    {
        std::lock_guard Lock(Mutex);
        LegacySessions.erase(std::string(PeerId));
        CancelPeerRequestsLocked(PeerId);
    }

    void CancelPeerRequests(std::string_view PeerId)
    {
        std::lock_guard Lock(Mutex);
        CancelPeerRequestsLocked(PeerId);
    }

    void CancelPeerRequestsLocked(std::string_view PeerId)
    {
        const std::string Prefix = std::string(PeerId) + '\n';
        for (auto& [Key, State] : InFlight)
        {
            if (Key.rfind(Prefix, 0) == 0)
                State->bCancelled.store(true, std::memory_order_relaxed);
        }
    }

    std::size_t LegacySessionCount() const
    {
        std::lock_guard Lock(Mutex);
        return LegacySessions.size();
    }

    std::size_t InFlightCount() const
    {
        std::lock_guard Lock(Mutex);
        return InFlight.size();
    }

private:
    FMcpMessageResult HandleLegacyInitialize(
        const FMcpRequestContext& Context,
        const FJson& Request)
    {
        const FJson Id = Request["id"];
        if (Context.PeerId.empty())
            return Error(Id, InvalidParams, "Legacy initialize requires a peer identity");
        if (!Request.contains("params") || !Request["params"].is_object())
            return Error(Id, InvalidParams, "initialize params must be an object");

        const FJson& Params = Request["params"];
        if (!Params.contains("protocolVersion")
            || !Params["protocolVersion"].is_string())
        {
            return Error(Id, InvalidParams, "initialize requires protocolVersion");
        }

        {
            std::lock_guard Lock(Mutex);
            if (LegacySessions.contains(Context.PeerId))
                return Error(Id, InvalidRequest, "Peer is already initialized");
            if (LegacySessions.size() >= Limits.MaxLegacySessions)
                return Error(Id, InvalidRequest, "Legacy session limit reached");
            LegacySessions.emplace(
                Context.PeerId, ELegacyState::InitializeResponded);
        }

        return Response(Id, {
            {"protocolVersion", McpProtocolLegacy},
            {"capabilities", {{"tools", FJson::object()}}},
            {"serverInfo", {{"name", ServerInfo.Name},
                {"version", ServerInfo.Version}}},
            {"instructions", ServerInfo.Instructions}});
    }

    void HandleLegacyInitialized(const FMcpRequestContext& Context)
    {
        std::lock_guard Lock(Mutex);
        const auto It = LegacySessions.find(Context.PeerId);
        if (It != LegacySessions.end())
            It->second = ELegacyState::Ready;
    }

    bool IsLegacyReady(const std::string& PeerId) const
    {
        std::lock_guard Lock(Mutex);
        const auto It = LegacySessions.find(PeerId);
        return It != LegacySessions.end() && It->second == ELegacyState::Ready;
    }

    FMcpMessageResult HandleReadyRequest(
        const FMcpRequestContext& Context,
        const FJson& Request,
        bool bModern)
    {
        const FJson& Id = Request["id"];
        const std::string Method = Request["method"].get<std::string>();
        if (Method == "server/discover")
        {
            FJson Result {
                {"resultType", "complete"},
                {"supportedVersions", {McpProtocolModern, McpProtocolLegacy}},
                {"capabilities", {{"tools", FJson::object()}}},
                {"_meta", ServerMeta(ServerInfo)},
                {"instructions", ServerInfo.Instructions}};
            return Response(Id, std::move(Result));
        }
        if (Method == "ping")
        {
            FJson Result = FJson::object();
            if (bModern)
            {
                Result["resultType"] = "complete";
                Result["_meta"] = ServerMeta(ServerInfo);
            }
            return Response(Id, std::move(Result));
        }
        if (Method == "tools/list")
            return HandleToolsList(Id, bModern);
        if (Method == "tools/call")
            return HandleToolsCall(Context, Request, bModern);
        return Error(Id, MethodNotFound, "Method not found: " + Method);
    }

    FMcpMessageResult HandleToolsList(const FJson& Id, bool bModern)
    {
        try
        {
            std::vector<FMcpToolDescriptor> Tools = ToolProvider.ListTools();
            std::sort(Tools.begin(), Tools.end(),
                [](const auto& Left, const auto& Right) {
                    return Left.Name < Right.Name;
                });
            FJson ToolList = FJson::array();
            for (const FMcpToolDescriptor& Tool : Tools)
            {
                FJson Entry {
                    {"name", Tool.Name},
                    {"description", Tool.Description},
                    {"inputSchema", ParseSchema(Tool.InputSchemaJson)}};
                if (!Tool.Title.empty())
                    Entry["title"] = Tool.Title;
                if (!Tool.OutputSchemaJson.empty())
                    Entry["outputSchema"] = ParseSchema(Tool.OutputSchemaJson);
                ToolList.push_back(std::move(Entry));
            }
            FJson Result {{"tools", std::move(ToolList)}};
            if (bModern)
            {
                Result["resultType"] = "complete";
                Result["ttlMs"] = 1000;
                Result["cacheScope"] = "private";
                Result["_meta"] = ServerMeta(ServerInfo);
            }
            return Response(Id, std::move(Result));
        }
        catch (const std::exception& Exception)
        {
            return Error(Id, InternalError, Exception.what());
        }
    }

    FMcpMessageResult HandleToolsCall(
        const FMcpRequestContext& Context,
        const FJson& Request,
        bool bModern)
    {
        const FJson& Id = Request["id"];
        if (!Request.contains("params") || !Request["params"].is_object())
            return Error(Id, InvalidParams, "tools/call params must be an object");
        const FJson& Params = Request["params"];
        if (!Params.contains("name") || !Params["name"].is_string())
            return Error(Id, InvalidParams, "tools/call requires a tool name");
        if (Params.contains("arguments") && !Params["arguments"].is_object())
            return Error(Id, InvalidParams, "Tool arguments must be an object");

        const std::string Name = Params["name"].get<std::string>();
        if (Params.contains("requestState") != Params.contains("inputResponses"))
        {
            return Error(Id, InvalidParams,
                "requestState and inputResponses must be provided together");
        }
        if (Params.contains("requestState")
            && (!Params["requestState"].is_string()
                || Params["requestState"].get_ref<const std::string&>().empty()
                || Params["requestState"].get_ref<const std::string&>().size() > 256
                || !Params["inputResponses"].is_object()))
        {
            return Error(Id, InvalidParams, "Invalid elicitation continuation");
        }
        try
        {
            const auto Tools = ToolProvider.ListTools();
            if (std::none_of(Tools.begin(), Tools.end(),
                [&Name](const FMcpToolDescriptor& Tool) {
                    return Tool.Name == Name;
                }))
            {
                return Error(Id, InvalidParams, "Unknown tool: " + Name);
            }
        }
        catch (const std::exception& Exception)
        {
            return Error(Id, InternalError, Exception.what());
        }

        const std::string RequestKey = MakeRequestKey(Context.PeerId, Id);
        auto State = std::make_shared<FMcpCancellationToken::FState>();
        State->Deadline = std::chrono::steady_clock::now() + Limits.ToolTimeout;
        {
            std::lock_guard Lock(Mutex);
            std::size_t PeerCount = 0;
            const std::string Prefix = Context.PeerId + '\n';
            for (const auto& [Key, Unused] : InFlight)
            {
                (void)Unused;
                PeerCount += Key.rfind(Prefix, 0) == 0 ? 1 : 0;
            }
            if (PeerCount >= Limits.MaxInFlightRequestsPerPeer)
                return Error(Id, InvalidRequest, "In-flight request limit reached");
            if (!InFlight.emplace(RequestKey, State).second)
                return Error(Id, InvalidRequest, "Duplicate in-flight request id");
        }

        FMcpToolCallResult ToolResult;
        try
        {
            const std::string Arguments = Params.value(
                "arguments", FJson::object()).dump();
            FMcpToolCallContext ToolContext {Context.PeerId, Id.dump()};
            ToolContext.bSupportsFormElicitation = bModern
                && SupportsModernFormElicitation(Request);
            ToolContext.FormElicitation = Context.FormElicitation;
            if (ToolContext.FormElicitation)
                ToolContext.bSupportsFormElicitation = true;
            if (Params.contains("requestState"))
            {
                ToolContext.RequestState =
                    Params["requestState"].get<std::string>();
                ToolContext.InputResponsesJson = Params["inputResponses"].dump();
            }
            ToolResult = ToolProvider.CallTool(
                ToolContext,
                Name, Arguments, FMcpCancellationToken(State));
        }
        catch (const std::exception& Exception)
        {
            RemoveInFlight(RequestKey);
            return Error(Id, InternalError, Exception.what());
        }

        RemoveInFlight(RequestKey);
        if (State->bCancelled.load(std::memory_order_relaxed))
            return {};
        if (std::chrono::steady_clock::now() >= State->Deadline)
        {
            ToolResult.bIsError = true;
            ToolResult.Text = "Tool call timed out";
            ToolResult.StructuredContentJson.clear();
        }
        if (ToolResult.Text.size() + ToolResult.StructuredContentJson.size()
                + ToolResult.InputRequestsJson.size()
                + ToolResult.RequestState.size()
            > Limits.MaxToolResultBytes)
        {
            ToolResult.bIsError = true;
            ToolResult.Text = "Tool result exceeds size limit";
            ToolResult.StructuredContentJson.clear();
            ToolResult.bInputRequired = false;
            ToolResult.InputRequestsJson.clear();
            ToolResult.RequestState.clear();
        }
        if (ToolResult.bInputRequired)
        {
            if (!bModern || !SupportsModernFormElicitation(Request))
                return Error(Id, InternalError,
                    "Tool requested input from a client without elicitation support");
            FJson InputRequests = FJson::parse(
                ToolResult.InputRequestsJson, nullptr, false);
            if (ToolResult.RequestState.empty() || InputRequests.is_discarded()
                || !InputRequests.is_object() || InputRequests.empty())
            {
                return Error(Id, InternalError,
                    "Tool returned an invalid input-required result");
            }
            return Response(Id, {{"resultType", "input_required"},
                {"_meta", ServerMeta(ServerInfo)},
                {"inputRequests", std::move(InputRequests)},
                {"requestState", ToolResult.RequestState}});
        }

        FJson Result {
            {"content", FJson::array({{{"type", "text"},
                {"text", ToolResult.Text}}})},
            {"isError", ToolResult.bIsError}};
        if (bModern)
        {
            Result["resultType"] = "complete";
            Result["_meta"] = ServerMeta(ServerInfo);
        }
        if (!ToolResult.StructuredContentJson.empty())
        {
            FJson Structured = FJson::parse(
                ToolResult.StructuredContentJson, nullptr, false);
            if (Structured.is_discarded())
                return Error(Id, InternalError, "Tool returned invalid structured content");
            Result["structuredContent"] = std::move(Structured);
        }
        return Response(Id, std::move(Result));
    }

    void HandleCancellation(
        const FMcpRequestContext& Context,
        const FJson& Request)
    {
        if (!Request.contains("params") || !Request["params"].is_object()
            || !Request["params"].contains("requestId")
            || !IsValidId(Request["params"]["requestId"]))
        {
            return;
        }
        const std::string Key = MakeRequestKey(
            Context.PeerId, Request["params"]["requestId"]);
        std::lock_guard Lock(Mutex);
        const auto It = InFlight.find(Key);
        if (It != InFlight.end())
            It->second->bCancelled.store(true, std::memory_order_relaxed);
    }

    static std::string MakeRequestKey(
        std::string_view PeerId,
        const FJson& Id)
    {
        return std::string(PeerId) + '\n' + Id.dump();
    }

    void RemoveInFlight(const std::string& Key)
    {
        std::lock_guard Lock(Mutex);
        InFlight.erase(Key);
    }

    IMcpToolProvider& ToolProvider;
    FMcpServerInfo ServerInfo;
    FMcpLimits Limits;
    mutable std::mutex Mutex;
    std::unordered_map<std::string, ELegacyState> LegacySessions;
    std::unordered_map<std::string,
        std::shared_ptr<FMcpCancellationToken::FState>> InFlight;
};

FMcpServerCore::FMcpServerCore(
    IMcpToolProvider& InToolProvider,
    FMcpServerInfo InServerInfo,
    FMcpLimits InLimits)
    : Impl(std::make_unique<FImpl>(
        InToolProvider, std::move(InServerInfo), InLimits))
{
}

FMcpServerCore::~FMcpServerCore() = default;

FMcpMessageResult FMcpServerCore::HandleMessage(
    const FMcpRequestContext& Context,
    std::string_view MessageJson)
{
    return Impl->Handle(Context, MessageJson);
}

void FMcpServerCore::ClosePeer(std::string_view PeerId)
{
    Impl->ClosePeer(PeerId);
}

void FMcpServerCore::CancelPeerRequests(std::string_view PeerId)
{
    Impl->CancelPeerRequests(PeerId);
}

std::size_t FMcpServerCore::GetLegacySessionCount() const
{
    return Impl->LegacySessionCount();
}

std::size_t FMcpServerCore::GetInFlightRequestCount() const
{
    return Impl->InFlightCount();
}
}
