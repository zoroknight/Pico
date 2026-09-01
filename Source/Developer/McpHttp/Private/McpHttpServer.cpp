#include "Pico/McpHttp/McpHttpServer.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <future>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_set>
#include <utility>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;
constexpr int HeaderMismatch = -32020;

std::string Lower(std::string_view Text)
{
    std::string Result(Text);
    std::transform(Result.begin(), Result.end(), Result.begin(),
        [](unsigned char Character)
        {
            return static_cast<char>(std::tolower(Character));
        });
    return Result;
}

std::string_view Trim(std::string_view Text)
{
    while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())))
        Text.remove_prefix(1);
    while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())))
        Text.remove_suffix(1);
    return Text;
}

std::string Header(
    const std::unordered_map<std::string, std::string>& Headers,
    std::string_view Name)
{
    const auto Found = Headers.find(Lower(Name));
    return Found == Headers.end() ? std::string {} : Found->second;
}

bool ContainsMediaType(std::string_view HeaderValue, std::string_view Type)
{
    const std::string LowerValue = Lower(HeaderValue);
    std::size_t Begin = 0;
    while (Begin <= LowerValue.size())
    {
        const std::size_t End = LowerValue.find(',', Begin);
        std::string_view Entry = Trim(std::string_view(LowerValue).substr(
            Begin, End == std::string::npos ? std::string::npos : End - Begin));
        const std::size_t Parameters = Entry.find(';');
        if (Trim(Entry.substr(0, Parameters)) == Type) return true;
        if (End == std::string::npos) break;
        Begin = End + 1;
    }
    return false;
}

bool ConstantTimeEqual(std::string_view Left, std::string_view Right)
{
    std::size_t Difference = Left.size() ^ Right.size();
    const std::size_t Count = std::max(Left.size(), Right.size());
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        const unsigned char L = Index < Left.size()
            ? static_cast<unsigned char>(Left[Index]) : 0;
        const unsigned char R = Index < Right.size()
            ? static_cast<unsigned char>(Right[Index]) : 0;
        Difference |= static_cast<std::size_t>(L ^ R);
    }
    return Difference == 0;
}

bool IsLoopbackHost(std::string_view Host, std::uint16_t Port)
{
    const std::string LowerHost = Lower(Trim(Host));
    const std::string PortText = std::to_string(Port);
    return LowerHost == "127.0.0.1:" + PortText
        || LowerHost == "localhost:" + PortText;
}

bool IsAllowedOrigin(std::string_view Origin, std::uint16_t)
{
    if (Origin.empty()) return true;
    const std::string LowerOrigin = Lower(Trim(Origin));
    for (const std::string_view Prefix : {
            std::string_view("http://127.0.0.1"),
            std::string_view("http://localhost")})
    {
        if (LowerOrigin == Prefix) return true;
        if (!LowerOrigin.starts_with(Prefix) || LowerOrigin.size() <= Prefix.size()
            || LowerOrigin[Prefix.size()] != ':')
            continue;
        const std::string_view PortText = std::string_view(LowerOrigin).substr(
            Prefix.size() + 1);
        if (!PortText.empty() && std::all_of(PortText.begin(), PortText.end(),
                [](unsigned char Character) { return std::isdigit(Character); }))
            return true;
    }
    return false;
}

FJson RequestId(const FJson& Request)
{
    return Request.is_object() && Request.contains("id")
        ? Request["id"] : FJson(nullptr);
}

FMcpHttpResponse JsonError(int Status, const FJson& Id,
    int Code, std::string Message)
{
    return {Status, {{"content-type", "application/json; charset=utf-8"}},
        FJson { {"jsonrpc", "2.0"}, {"id", Id},
            {"error", {{"code", Code}, {"message", std::move(Message)}}}
        }.dump()};
}

FMcpHttpResponse PlainError(int Status, std::string Message)
{
    return {Status, {{"content-type", "text/plain; charset=utf-8"}},
        std::move(Message)};
}

bool TryReadBodyMetadata(const FJson& Request,
    std::string& OutMethod,
    std::string& OutVersion,
    std::string& OutName)
{
    if (!Request.is_object() || !Request.contains("method")
        || !Request["method"].is_string())
        return false;
    OutMethod = Request["method"].get<std::string>();
    if (!Request.contains("params") || !Request["params"].is_object())
        return false;
    const FJson& Params = Request["params"];
    if (!Params.contains("_meta") || !Params["_meta"].is_object())
        return false;
    constexpr std::string_view VersionKey =
        "io.modelcontextprotocol/protocolVersion";
    const FJson& Meta = Params["_meta"];
    if (!Meta.contains(VersionKey) || !Meta[VersionKey].is_string())
        return false;
    OutVersion = Meta[VersionKey].get<std::string>();
    if (OutMethod == "tools/call")
    {
        if (!Params.contains("name") || !Params["name"].is_string())
            return false;
        OutName = Params["name"].get<std::string>();
    }
    return true;
}

bool TryReadRequestMethod(const FJson& Request, std::string& OutMethod)
{
    if (!Request.is_object() || !Request.contains("method")
        || !Request["method"].is_string())
        return false;
    OutMethod = Request["method"].get<std::string>();
    return true;
}

bool IsStandardMethod(std::string_view Method)
{
    return Method == "initialize" || Method == "notifications/initialized"
        || Method == "notifications/cancelled" || Method == "ping"
        || Method == "tools/list" || Method == "tools/call";
}

bool IsModernRequest(const FJson& Request)
{
    std::string Method;
    std::string Version;
    std::string Name;
    return TryReadBodyMetadata(Request, Method, Version, Name);
}

bool IsSafeSessionId(std::string_view SessionId)
{
    return !SessionId.empty() && SessionId.size() <= 128
        && std::all_of(SessionId.begin(), SessionId.end(),
            [](unsigned char Character)
            {
                return std::isalnum(Character) || Character == '-'
                    || Character == '_' || Character == '.';
            });
}

bool IsHeaderSafeAscii(std::string_view Value)
{
    if (Value.empty() || std::isspace(static_cast<unsigned char>(Value.front()))
        || std::isspace(static_cast<unsigned char>(Value.back())))
        return false;
    return std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
    {
        return Character >= 0x20 && Character <= 0x7e
            && Character != '\r' && Character != '\n';
    });
}

int Base64Value(unsigned char Character)
{
    if (Character >= 'A' && Character <= 'Z') return Character - 'A';
    if (Character >= 'a' && Character <= 'z') return Character - 'a' + 26;
    if (Character >= '0' && Character <= '9') return Character - '0' + 52;
    if (Character == '+') return 62;
    if (Character == '/') return 63;
    return -1;
}

bool TryDecodeHeaderValue(std::string_view Value, std::string& OutValue)
{
    constexpr std::string_view Prefix = "=?base64?";
    constexpr std::string_view Suffix = "?=";
    if (!Value.starts_with(Prefix) || !Value.ends_with(Suffix))
    {
        if (!IsHeaderSafeAscii(Value)) return false;
        OutValue = std::string(Value);
        return true;
    }
    Value.remove_prefix(Prefix.size());
    Value.remove_suffix(Suffix.size());
    if (Value.empty() || Value.size() % 4 != 0) return false;
    OutValue.clear();
    for (std::size_t Index = 0; Index < Value.size(); Index += 4)
    {
        std::array<int, 4> Digits {};
        for (std::size_t Offset = 0; Offset < 4; ++Offset)
        {
            Digits[Offset] = Value[Index + Offset] == '='
                ? -2 : Base64Value(static_cast<unsigned char>(Value[Index + Offset]));
            if (Digits[Offset] == -1) return false;
        }
        if (Digits[0] < 0 || Digits[1] < 0
            || (Digits[2] == -2 && Digits[3] != -2)) return false;
        const unsigned Value24 = (static_cast<unsigned>(Digits[0]) << 18)
            | (static_cast<unsigned>(Digits[1]) << 12)
            | (Digits[2] < 0 ? 0u : static_cast<unsigned>(Digits[2]) << 6)
            | (Digits[3] < 0 ? 0u : static_cast<unsigned>(Digits[3]));
        OutValue.push_back(static_cast<char>((Value24 >> 16) & 0xff));
        if (Digits[2] >= 0) OutValue.push_back(static_cast<char>((Value24 >> 8) & 0xff));
        if (Digits[3] >= 0) OutValue.push_back(static_cast<char>(Value24 & 0xff));
        if ((Digits[2] == -2 || Digits[3] == -2) && Index + 4 != Value.size())
            return false;
    }
    return true;
}
}

FMcpHttpBinding::FMcpHttpBinding(
    FMcpServerCore& InCore,
    FMcpHttpServerConfig InConfig)
    : Core(InCore), Config(std::move(InConfig))
{
}

FMcpHttpResponse FMcpHttpBinding::Handle(
    const FMcpHttpRequest& Request,
    std::string_view PeerId)
{
    if (std::optional<FMcpHttpResponse> Rejection = Validate(Request))
        return std::move(*Rejection);
    return ExecuteValidated(Request, PeerId);
}

std::optional<FMcpHttpResponse> FMcpHttpBinding::Validate(
    const FMcpHttpRequest& Request)
{
    auto Reject = [this](FMcpHttpResponse Response)
    {
        RejectedRequests.fetch_add(1, std::memory_order_relaxed);
        return std::optional<FMcpHttpResponse>(std::move(Response));
    };

    if (Request.Path != Config.Endpoint)
        return Reject(PlainError(404, "Not found"));
    if (Request.Method != "POST")
    {
        FMcpHttpResponse Response = PlainError(405, "Method not allowed");
        Response.Headers["allow"] = "POST";
        return Reject(std::move(Response));
    }
    if (!IsLoopbackHost(Header(Request.Headers, "host"), Config.Port))
        return Reject(PlainError(400, "Invalid Host header"));
    if (!IsAllowedOrigin(Header(Request.Headers, "origin"), Config.Port))
        return Reject(PlainError(403, "Forbidden Origin"));

    const std::string Authorization = Header(Request.Headers, "authorization");
    constexpr std::string_view BearerPrefix = "Bearer ";
    if (!Authorization.starts_with(BearerPrefix)
        || !ConstantTimeEqual(std::string_view(Authorization).substr(
                BearerPrefix.size()), Config.BearerToken))
    {
        FMcpHttpResponse Response = PlainError(401, "Unauthorized");
        Response.Headers["www-authenticate"] = "Bearer";
        return Reject(std::move(Response));
    }
    if (Request.Body.size() > Config.MaxRequestBytes)
        return Reject(PlainError(413, "Request body too large"));
    if (!ContainsMediaType(Header(Request.Headers, "content-type"),
            "application/json"))
        return Reject(PlainError(415, "Content-Type must be application/json"));
    const std::string Accept = Header(Request.Headers, "accept");
    if (!ContainsMediaType(Accept, "application/json")
        || !ContainsMediaType(Accept, "text/event-stream"))
        return Reject(PlainError(406,
            "Accept must include application/json and text/event-stream"));

    const FJson Body = FJson::parse(Request.Body, nullptr, false);
    if (Body.is_discarded())
        return Reject(JsonError(400, nullptr, -32700, "Parse error"));
    std::string BodyMethod;
    std::string BodyVersion;
    std::string BodyName;
    if (!TryReadRequestMethod(Body, BodyMethod))
        return Reject(JsonError(400, RequestId(Body), -32600,
            "Invalid JSON-RPC request"));

    const bool bModern = TryReadBodyMetadata(
        Body, BodyMethod, BodyVersion, BodyName);
    if (!bModern)
    {
        if (!IsStandardMethod(BodyMethod))
            return Reject(JsonError(404, RequestId(Body), -32601,
                "Method not found"));
        return std::nullopt;
    }

    const std::string ProtocolVersion = Header(
        Request.Headers, "mcp-protocol-version");
    const std::string Method = Header(Request.Headers, "mcp-method");
    if (ProtocolVersion.empty() || ProtocolVersion != BodyVersion)
        return Reject(JsonError(400, RequestId(Body), HeaderMismatch,
            "MCP-Protocol-Version does not match the request body"));
    if (Method.empty() || Method != BodyMethod)
        return Reject(JsonError(400, RequestId(Body), HeaderMismatch,
            "Mcp-Method does not match the request body"));
    if (BodyMethod == "tools/call")
    {
        std::string DecodedName;
        if (!TryDecodeHeaderValue(Header(Request.Headers, "mcp-name"), DecodedName)
            || DecodedName != BodyName)
            return Reject(JsonError(400, RequestId(Body), HeaderMismatch,
                "Mcp-Name does not match the request body"));
    }

    if (BodyVersion != McpProtocolModern)
        return Reject(JsonError(400, RequestId(Body), -32022,
            "Unsupported protocol version; supported: 2026-07-28"));
    if (BodyMethod != "server/discover" && BodyMethod != "ping"
        && BodyMethod != "tools/list" && BodyMethod != "tools/call")
        return Reject(JsonError(404, RequestId(Body), -32601,
            "Method not found"));
    return std::nullopt;
}

FMcpHttpResponse FMcpHttpBinding::ExecuteValidated(
    const FMcpHttpRequest& Request,
    std::string_view PeerId)
{
    struct FActiveGuard
    {
        explicit FActiveGuard(std::atomic<std::uint64_t>& InValue) : Value(InValue)
        { Value.fetch_add(1, std::memory_order_relaxed); }
        ~FActiveGuard() { Value.fetch_sub(1, std::memory_order_relaxed); }
        std::atomic<std::uint64_t>& Value;
    } Guard(ActiveRequests);

    AcceptedRequests.fetch_add(1, std::memory_order_relaxed);
    const FMcpMessageResult Result = Core.HandleMessage(
        {std::string(PeerId)}, Request.Body);
    if (!Result.bHasResponse)
        return {202, {}, {}};

    int Status = 200;
    const FJson ResponseBody = FJson::parse(Result.ResponseJson, nullptr, false);
    if (ResponseBody.is_object() && ResponseBody.contains("error")
        && ResponseBody["error"].is_object())
    {
        const int Code = ResponseBody["error"].value("code", 0);
        if (Code == -32601) Status = 404;
        else if (Code == -32022 || Code == HeaderMismatch) Status = 400;
    }
    return {Status, {{"content-type", "application/json; charset=utf-8"}},
        Result.ResponseJson};
}

FMcpHttpServerStatus FMcpHttpBinding::GetStatus() const
{
    FMcpHttpServerStatus Status;
    Status.BindAddress = Config.BindAddress;
    Status.Port = Config.Port;
    Status.Endpoint = Config.Endpoint;
    Status.AcceptedRequests = AcceptedRequests.load(std::memory_order_relaxed);
    Status.RejectedRequests = RejectedRequests.load(std::memory_order_relaxed);
    Status.ActiveRequests = ActiveRequests.load(std::memory_order_relaxed);
    return Status;
}

const FMcpHttpServerConfig& FMcpHttpBinding::GetConfig() const
{
    return Config;
}

class FMcpHttpServer::FImpl
{
public:
    explicit FImpl(FMcpServerCore& InCore) : Core(InCore) {}

    std::string CreateSessionId() const
    {
        return GenerateMcpBearerToken(16);
    }

    void RegisterSession(std::string SessionId)
    {
        std::lock_guard Lock(SessionMutex);
        Sessions.insert(std::move(SessionId));
    }

    bool HasSession(std::string_view SessionId) const
    {
        std::lock_guard Lock(SessionMutex);
        return Sessions.contains(std::string(SessionId));
    }

    std::size_t GetSessionCount() const
    {
        std::lock_guard Lock(SessionMutex);
        return Sessions.size();
    }

    void CloseSessions()
    {
        std::unordered_set<std::string> Closing;
        {
            std::lock_guard Lock(SessionMutex);
            Closing.swap(Sessions);
        }
        for (const std::string& SessionId : Closing)
            Core.ClosePeer("http-session-" + SessionId);
    }

    FMcpServerCore& Core;
    std::unique_ptr<FMcpHttpBinding> Binding;
    std::unique_ptr<httplib::Server> Server;
    std::thread ListenerThread;
    std::atomic<bool> bRunning {false};
    std::atomic<std::uint64_t> NextPeer {1};
    mutable std::mutex SessionMutex;
    std::unordered_set<std::string> Sessions;
    mutable std::mutex Mutex;
    std::string LastError;
    FMcpHttpServerStatus LastStatus;
};

FMcpHttpServer::FMcpHttpServer(FMcpServerCore& InCore)
    : Impl(std::make_unique<FImpl>(InCore))
{
}

FMcpHttpServer::~FMcpHttpServer()
{
    Stop();
}

bool FMcpHttpServer::Start(FMcpHttpServerConfig Config, std::string* OutError)
{
    Stop();
    auto Fail = [this, OutError](std::string Error)
    {
        {
            std::lock_guard Lock(Impl->Mutex);
            Impl->LastError = Error;
        }
        if (OutError) *OutError = std::move(Error);
        return false;
    };
    if (Config.BindAddress != "127.0.0.1")
        return Fail("MCP HTTP server may only bind to 127.0.0.1");
    if (Config.Port == 0)
        return Fail("MCP HTTP port must be between 1 and 65535");
    if (Config.Endpoint.empty() || Config.Endpoint.front() != '/'
        || Config.Endpoint.find_first_of("?#\r\n") != std::string::npos)
        return Fail("MCP HTTP endpoint must be an absolute path");
    if (Config.BearerToken.size() < 32)
        return Fail("MCP bearer token must contain at least 32 characters");

    {
        std::lock_guard Lock(Impl->Mutex);
        Impl->LastError.clear();
        Impl->LastStatus = {};
    }

    Impl->Binding = std::make_unique<FMcpHttpBinding>(Impl->Core, Config);
    Impl->Server = std::make_unique<httplib::Server>();
    Impl->Server->set_payload_max_length(Config.MaxRequestBytes);
    Impl->Server->set_read_timeout(30, 0);
    Impl->Server->set_write_timeout(30, 0);
    Impl->Server->set_idle_interval(0, 100000);
    const std::string Endpoint = Config.Endpoint;
    Impl->Server->Post(Endpoint,
        [this](const httplib::Request& Request, httplib::Response& Response)
        {
            FMcpHttpRequest Input;
            Input.Method = Request.method;
            Input.Path = Request.path;
            Input.Body = Request.body;
            for (const auto& [Name, Value] : Request.headers)
                Input.Headers[Lower(Name)] = Value;
            if (std::optional<FMcpHttpResponse> Rejection =
                    Impl->Binding->Validate(Input))
            {
                Response.status = Rejection->Status;
                for (const auto& [Name, Value] : Rejection->Headers)
                    Response.set_header(Name, Value);
                Response.body = std::move(Rejection->Body);
                return;
            }

            const FJson Body = FJson::parse(Input.Body, nullptr, false);
            std::string Method;
            TryReadRequestMethod(Body, Method);
            const bool bModern = IsModernRequest(Body);
            std::string SessionId;
            std::string PeerId;
            if (bModern)
            {
                PeerId = "http-" + std::to_string(
                    Impl->NextPeer.fetch_add(1));
            }
            else if (Method == "initialize")
            {
                SessionId = Impl->CreateSessionId();
                PeerId = "http-session-" + SessionId;
            }
            else
            {
                SessionId = Header(Input.Headers, "mcp-session-id");
                if (!IsSafeSessionId(SessionId))
                {
                    const FMcpHttpResponse Rejection = JsonError(
                        400, RequestId(Body), -32021,
                        "Missing or invalid MCP session");
                    Response.status = Rejection.Status;
                    for (const auto& [Name, Value] : Rejection.Headers)
                        Response.set_header(Name, Value);
                    Response.body = Rejection.Body;
                    return;
                }
                if (!Impl->HasSession(SessionId))
                {
                    const FMcpHttpResponse Rejection = JsonError(
                        404, RequestId(Body), -32021,
                        "MCP session expired; initialize a new session");
                    Response.status = Rejection.Status;
                    for (const auto& [Name, Value] : Rejection.Headers)
                        Response.set_header(Name, Value);
                    Response.body = Rejection.Body;
                    return;
                }
                PeerId = "http-session-" + SessionId;
            }
            if (Method == "tools/call")
            {
                struct FStreamState
                {
                    std::mutex Mutex;
                    std::future<FMcpHttpResponse> Future;
                    bool bStarted = false;
                    bool bFinished = false;
                };
                auto State = std::make_shared<FStreamState>();
                auto Binding = Impl->Binding.get();
                auto Core = &Impl->Core;
                Response.set_header("X-Accel-Buffering", "no");
                Response.set_chunked_content_provider("text/event-stream",
                    [State, Binding, Core, Input = std::move(Input), PeerId,
                        bModern](
                        std::size_t, httplib::DataSink& Sink) mutable
                    {
                        std::unique_lock Lock(State->Mutex);
                        if (State->bFinished) return false;
                        if (!State->bStarted)
                        {
                            State->bStarted = true;
                            State->Future = std::async(std::launch::async,
                                [Binding, Input, PeerId]() mutable
                                {
                                    return Binding->ExecuteValidated(Input, PeerId);
                                });
                        }
                        while (State->Future.wait_for(std::chrono::milliseconds(10))
                            != std::future_status::ready)
                        {
                            if (!Sink.is_writable())
                            {
                                if (bModern) Core->ClosePeer(PeerId);
                                else Core->CancelPeerRequests(PeerId);
                                State->bFinished = true;
                                Sink.done();
                                return false;
                            }
                        }
                        const FMcpHttpResponse Output = State->Future.get();
                        const std::string Event = "event: message\ndata: "
                            + Output.Body + "\n\n";
                        const bool bWritten = Sink.write(Event.data(), Event.size());
                        State->bFinished = true;
                        Sink.done();
                        if (bModern) Core->ClosePeer(PeerId);
                        return bWritten;
                    },
                    [Core, PeerId, bModern](bool bSuccess)
                    {
                        if (!bSuccess && bModern) Core->ClosePeer(PeerId);
                    });
                return;
            }

            const FMcpHttpResponse Output =
                Impl->Binding->ExecuteValidated(Input, PeerId);
            Response.status = Output.Status;
            for (const auto& [Name, Value] : Output.Headers)
                Response.set_header(Name, Value);
            if (!bModern && Method == "initialize" && Output.Status == 200)
            {
                Impl->RegisterSession(SessionId);
                Response.set_header("Mcp-Session-Id", SessionId);
            }
            else if (!bModern && Method == "initialize")
            {
                Impl->Core.ClosePeer(PeerId);
            }
            if (!Output.Body.empty()) Response.body = Output.Body;
            if (bModern) Impl->Core.ClosePeer(PeerId);
        });
    Impl->Server->Get(Endpoint,
        [](const httplib::Request&, httplib::Response& Response)
        {
            Response.status = 405;
            Response.set_header("Allow", "POST");
        });
    Impl->Server->Delete(Endpoint,
        [](const httplib::Request&, httplib::Response& Response)
        {
            Response.status = 405;
            Response.set_header("Allow", "POST");
        });

    if (!Impl->Server->bind_to_port(Config.BindAddress, Config.Port))
    {
        Impl->Server.reset();
        Impl->Binding.reset();
        return Fail("Could not bind the local MCP HTTP endpoint");
    }
    Impl->bRunning.store(true);
    Impl->ListenerThread = std::thread([this]
    {
        if (!Impl->Server->listen_after_bind())
        {
            std::lock_guard Lock(Impl->Mutex);
            if (Impl->bRunning.load())
                Impl->LastError = "MCP HTTP listener stopped unexpectedly";
        }
        Impl->bRunning.store(false);
    });
    return true;
}

void FMcpHttpServer::Stop()
{
    if (!Impl) return;
    Impl->bRunning.store(false);
    if (Impl->Server) Impl->Server->stop();
    if (Impl->ListenerThread.joinable()) Impl->ListenerThread.join();
    Impl->CloseSessions();
    if (Impl->Binding)
    {
        std::lock_guard Lock(Impl->Mutex);
        Impl->LastStatus = Impl->Binding->GetStatus();
    }
    Impl->Server.reset();
    Impl->Binding.reset();
}

bool FMcpHttpServer::IsRunning() const
{
    return Impl && Impl->bRunning.load();
}

FMcpHttpServerStatus FMcpHttpServer::GetStatus() const
{
    if (!Impl) return {};
    FMcpHttpServerStatus Status;
    {
        std::lock_guard Lock(Impl->Mutex);
        Status = Impl->Binding ? Impl->Binding->GetStatus() : Impl->LastStatus;
        Status.LastError = Impl->LastError;
    }
    Status.bRunning = Impl->bRunning.load();
    Status.ActiveSessions = static_cast<std::uint64_t>(
        Impl->GetSessionCount());
    return Status;
}

std::string GenerateMcpBearerToken(std::size_t ByteCount)
{
    ByteCount = std::max<std::size_t>(ByteCount, 16);
    std::random_device Random;
    constexpr char Hex[] = "0123456789abcdef";
    std::string Result;
    Result.resize(ByteCount * 2);
    for (std::size_t Index = 0; Index < ByteCount; ++Index)
    {
        const unsigned Value = Random() & 0xffu;
        Result[Index * 2] = Hex[(Value >> 4) & 0xf];
        Result[Index * 2 + 1] = Hex[Value & 0xf];
    }
    return Result;
}
}
