#include "TestRunner.h"

#include "Pico/Mcp/McpServerCore.h"
#include "Pico/McpHttp/McpHttpServer.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <atomic>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
using FJson = nlohmann::json;

class FProvider final : public Pico::IMcpToolProvider
{
public:
    std::vector<Pico::FMcpToolDescriptor> ListTools() const override
    {
        return {
            {"echo", "Echo", "Echo arguments",
                R"({"type":"object","additionalProperties":true})", ""},
            {"wait", "Wait", "Wait for cancellation",
                R"({"type":"object","additionalProperties":false})", ""},
            {"approve", "Approve", "Request client approval",
                R"({"type":"object","additionalProperties":false})", ""}};
    }

    Pico::FMcpToolCallResult CallTool(
        const Pico::FMcpToolCallContext& Context,
        std::string_view Name,
        std::string_view ArgumentsJson,
        const Pico::FMcpCancellationToken& Cancellation) override
    {
        ++Calls;
        if (Name == "wait")
        {
            const auto Deadline = std::chrono::steady_clock::now() + 2s;
            while (std::chrono::steady_clock::now() < Deadline)
            {
                if (Cancellation.IsCancellationRequested())
                {
                    bCancellationObserved.store(true);
                    return {true, "cancelled", ""};
                }
                std::this_thread::sleep_for(5ms);
            }
            return {false, "completed", "{}"};
        }
        if (Name == "approve")
        {
            bElicitationObserved.store(static_cast<bool>(Context.FormElicitation));
            if (!Context.FormElicitation)
                return {true, "elicitation unavailable", ""};
            const auto Response = Context.FormElicitation({
                "Approve the test mutation",
                R"({"type":"object","properties":{},"additionalProperties":false})",
                R"({"codex_approval_kind":"mcp_tool_call","tool_name":"approve"})"});
            if (!Response || Response->Action != "accept")
                return {true, "approval rejected", ""};
            ApprovedSideEffects.fetch_add(1);
            return {false, "approved", R"({"changed":true})"};
        }
        if (Name != "echo") return {true, "unknown", ""};
        return {false, "echo", std::string(ArgumentsJson)};
    }

    int Calls = 0;
    std::atomic<bool> bCancellationObserved {false};
    std::atomic<bool> bElicitationObserved {false};
    std::atomic<int> ApprovedSideEffects {0};
};

FJson ModernRequest(int Id, std::string Method, FJson Params = FJson::object())
{
    Params["_meta"] = {
        {"io.modelcontextprotocol/protocolVersion", Pico::McpProtocolModern},
        {"io.modelcontextprotocol/clientCapabilities", FJson::object()}};
    return {{"jsonrpc", "2.0"}, {"id", Id},
        {"method", std::move(Method)}, {"params", std::move(Params)}};
}

Pico::FMcpHttpRequest Request(
    const FJson& Body,
    std::string MethodHeader,
    std::string NameHeader = {})
{
    Pico::FMcpHttpRequest Value;
    Value.Method = "POST";
    Value.Path = "/mcp";
    Value.Body = Body.dump();
    Value.Headers = {
        {"host", "127.0.0.1:18765"},
        {"authorization", "Bearer 0123456789abcdef0123456789abcdef"},
        {"content-type", "application/json; charset=utf-8"},
        {"accept", "application/json, text/event-stream"},
        {"mcp-protocol-version", std::string(Pico::McpProtocolModern)},
        {"mcp-method", std::move(MethodHeader)}};
    if (!NameHeader.empty()) Value.Headers["mcp-name"] = std::move(NameHeader);
    return Value;
}

Pico::FMcpHttpRequest StandardRequest(const FJson& Body)
{
    Pico::FMcpHttpRequest Value;
    Value.Method = "POST";
    Value.Path = "/mcp";
    Value.Body = Body.dump();
    Value.Headers = {
        {"host", "127.0.0.1:18765"},
        {"authorization", "Bearer 0123456789abcdef0123456789abcdef"},
        {"content-type", "application/json; charset=utf-8"},
        {"accept", "application/json, text/event-stream"}};
    return Value;
}

void TestBinding(FTestRunner& Runner)
{
    FProvider Provider;
    Pico::FMcpServerCore Core(Provider);
    Pico::FMcpHttpServerConfig Config;
    Config.Port = 18765;
    Config.BearerToken = "0123456789abcdef0123456789abcdef";
    Pico::FMcpHttpBinding Binding(Core, Config);

    const FJson Discover = ModernRequest(1, "server/discover");
    const Pico::FMcpHttpResponse Good = Binding.Handle(
        Request(Discover, "server/discover"), "binding-test");
    Runner.Expect(Good.Status == 200
            && FJson::parse(Good.Body)["result"]["resultType"] == "complete",
        "Streamable HTTP accepts a fully mirrored modern MCP request");

    Pico::FMcpHttpRequest Unauthorized = Request(Discover, "server/discover");
    Unauthorized.Headers.erase("authorization");
    Runner.Expect(Binding.Handle(Unauthorized, "unauthorized").Status == 401,
        "Streamable HTTP rejects missing bearer authentication");

    Pico::FMcpHttpRequest BadOrigin = Request(Discover, "server/discover");
    BadOrigin.Headers["origin"] = "https://attacker.example";
    Runner.Expect(Binding.Handle(BadOrigin, "bad-origin").Status == 403,
        "Streamable HTTP rejects non-loopback Origin values");

    Pico::FMcpHttpRequest BadHost = Request(Discover, "server/discover");
    BadHost.Headers["host"] = "attacker.example:18765";
    Runner.Expect(Binding.Handle(BadHost, "bad-host").Status == 400,
        "Streamable HTTP rejects non-loopback Host values");

    Pico::FMcpHttpRequest MissingAccept = Request(Discover, "server/discover");
    MissingAccept.Headers["accept"] = "application/json";
    Runner.Expect(Binding.Handle(MissingAccept, "missing-accept").Status == 406,
        "Streamable HTTP requires JSON and SSE response support");

    Pico::FMcpHttpRequest HeaderMismatchRequest = Request(
        ModernRequest(2, "tools/call", {
            {"name", "echo"}, {"arguments", {{"value", 7}}}}),
        "tools/call", "wrong-name");
    const Pico::FMcpHttpResponse Mismatch = Binding.Handle(
        HeaderMismatchRequest, "header-mismatch");
    Runner.Expect(Mismatch.Status == 400
            && FJson::parse(Mismatch.Body)["error"]["code"] == -32020
            && Provider.Calls == 0,
        "HeaderMismatch is rejected before a tool can execute");

    const Pico::FMcpHttpResponse ToolResult = Binding.Handle(Request(
        ModernRequest(3, "tools/call", {
            {"name", "echo"}, {"arguments", {{"value", 7}}}}),
        "tools/call", "echo"), "tool-call");
    Runner.Expect(ToolResult.Status == 200 && Provider.Calls == 1
            && FJson::parse(ToolResult.Body)["result"]["isError"] == false,
        "Validated Streamable HTTP tool calls reach MCP Core exactly once");

    const std::string StandardPeer = "standard-binding-session";
    const Pico::FMcpHttpResponse Initialize = Binding.Handle(StandardRequest({
        {"jsonrpc", "2.0"}, {"id", 20}, {"method", "initialize"},
        {"params", {{"protocolVersion", "2026-07-28"},
            {"capabilities", FJson::object()},
            {"clientInfo", {{"name", "codex-test"}, {"version", "1"}}}}}}),
        StandardPeer);
    Runner.Expect(Initialize.Status == 200
            && FJson::parse(Initialize.Body)["result"]["protocolVersion"]
                == std::string(Pico::McpProtocolLegacy),
        "Standard Streamable HTTP initialize reaches the compatibility core");
    const Pico::FMcpHttpResponse Initialized = Binding.Handle(StandardRequest({
        {"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}),
        StandardPeer);
    const Pico::FMcpHttpResponse StandardTools = Binding.Handle(StandardRequest({
        {"jsonrpc", "2.0"}, {"id", 21}, {"method", "tools/list"},
        {"params", FJson::object()}}), StandardPeer);
    Runner.Expect(Initialized.Status == 202 && StandardTools.Status == 200
            && FJson::parse(StandardTools.Body)["result"]["tools"].size() == 3,
        "A standard initialized HTTP peer can list tools without custom metadata");

    const Pico::FMcpHttpResponse Unknown = Binding.Handle(Request(
        ModernRequest(4, "unknown/method"), "unknown/method"), "unknown");
    Runner.Expect(Unknown.Status == 404
            && FJson::parse(Unknown.Body)["error"]["code"] == -32601,
        "Unknown modern MCP methods map to HTTP 404 plus JSON-RPC MethodNotFound");

    const Pico::FMcpHttpServerStatus Status = Binding.GetStatus();
    Runner.Expect(Status.AcceptedRequests == 5
            && Status.RejectedRequests == 6
            && Status.ActiveRequests == 0,
        "HTTP diagnostics separate accepted, rejected, and active requests");
}

void TestRealLoopbackServer(FTestRunner& Runner)
{
    FProvider Provider;
    Pico::FMcpServerCore Core(Provider);
    Pico::FMcpHttpServer Server(Core);
    Pico::FMcpHttpServerConfig Config;
    Config.BearerToken = "abcdef0123456789abcdef0123456789";
    bool bStarted = false;
    for (int Port = 18870; Port < 18890 && !bStarted; ++Port)
    {
        Config.Port = static_cast<std::uint16_t>(Port);
        bStarted = Server.Start(Config);
    }
    Runner.Expect(bStarted, "The MCP HTTP server binds to a real loopback socket");
    if (!bStarted) return;

    std::this_thread::sleep_for(20ms);
    httplib::Client Client("127.0.0.1", Config.Port);
    Client.set_connection_timeout(2, 0);
    Client.set_read_timeout(2, 0);
    const FJson Body = ModernRequest(10, "server/discover");
    httplib::Headers Headers = {
        {"Authorization", "Bearer " + Config.BearerToken},
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", std::string(Pico::McpProtocolModern)},
        {"Mcp-Method", "server/discover"}};
    const auto Result = Client.Post("/mcp", Headers, Body.dump(), "application/json");
    Runner.Expect(Result && Result->status == 200
            && FJson::parse(Result->body)["result"]["resultType"] == "complete",
        "An independent HTTP client completes a modern MCP request over TCP");

    httplib::Headers StandardHeaders = {
        {"Authorization", "Bearer " + Config.BearerToken},
        {"Accept", "application/json, text/event-stream"}};
    const FJson InitializeBody = {
        {"jsonrpc", "2.0"}, {"id", 30}, {"method", "initialize"},
        {"params", {{"protocolVersion", "2026-07-28"},
            {"capabilities", {{"elicitation", {{"form", FJson::object()}}}}},
            {"clientInfo", {{"name", "codex-test"}, {"version", "1"}}}}}};
    const auto InitializeResult = Client.Post(
        "/mcp", StandardHeaders, InitializeBody.dump(), "application/json");
    const std::string SessionId = InitializeResult
        ? InitializeResult->get_header_value("Mcp-Session-Id") : std::string {};
    StandardHeaders.emplace("Mcp-Session-Id", SessionId);
    const auto InitializedResult = Client.Post(
        "/mcp", StandardHeaders,
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
        "application/json");
    const auto StandardToolsResult = Client.Post(
        "/mcp", StandardHeaders,
        R"({"jsonrpc":"2.0","id":31,"method":"tools/list","params":{}})",
        "application/json");
    Runner.Expect(InitializeResult && InitializeResult->status == 200
            && !SessionId.empty() && SessionId.size() <= 128
            && InitializedResult && InitializedResult->status == 202
            && StandardToolsResult && StandardToolsResult->status == 200
            && FJson::parse(StandardToolsResult->body)["result"]["tools"].size() == 3,
        "A real standard HTTP client completes session initialize and tools/list");

    httplib::Client ApprovalClient("127.0.0.1", Config.Port);
    ApprovalClient.set_connection_timeout(2, 0);
    ApprovalClient.set_read_timeout(5, 0);
    httplib::Client ApprovalResponseClient("127.0.0.1", Config.Port);
    ApprovalResponseClient.set_connection_timeout(2, 0);
    ApprovalResponseClient.set_read_timeout(2, 0);
    std::string ApprovalStream;
    bool bResponded = false;
    int ApprovalResponseStatus = 0;
    const auto ApprovalResult = ApprovalClient.Post(
        "/mcp", StandardHeaders,
        R"({"jsonrpc":"2.0","id":34,"method":"tools/call","params":{"name":"approve","arguments":{}}})",
        "application/json",
        [&](const char* Data, std::size_t Size)
        {
            ApprovalStream.append(Data, Size);
            const std::string Marker = "data: ";
            const std::size_t Begin = ApprovalStream.find(Marker);
            const std::size_t End = Begin == std::string::npos
                ? std::string::npos : ApprovalStream.find("\n\n", Begin);
            if (!bResponded && End != std::string::npos)
            {
                const FJson Event = FJson::parse(ApprovalStream.substr(
                    Begin + Marker.size(), End - Begin - Marker.size()),
                    nullptr, false);
                if (Event.is_object()
                    && Event.value("method", "") == "elicitation/create")
                {
                    bResponded = true;
                    const FJson Decision {
                        {"jsonrpc", "2.0"}, {"id", Event["id"]},
                        {"result", {{"action", "accept"},
                            {"content", FJson::object()}}}};
                    const auto DecisionResult = ApprovalResponseClient.Post(
                        "/mcp", StandardHeaders, Decision.dump(),
                        "application/json");
                    ApprovalResponseStatus = DecisionResult
                        ? DecisionResult->status : 0;
                }
            }
            return true;
        });
    Runner.Expect(ApprovalResult && ApprovalResult->status == 200
            && bResponded && ApprovalResponseStatus == 202
            && Provider.bElicitationObserved.load()
            && Provider.ApprovedSideEffects.load() == 1
            && ApprovalStream.find("\"isError\":false") != std::string::npos,
        "A standard Streamable HTTP client approves a tool through bidirectional Elicitation");

    httplib::Headers MissingSessionHeaders = {
        {"Authorization", "Bearer " + Config.BearerToken},
        {"Accept", "application/json, text/event-stream"}};
    const auto MissingSessionResult = Client.Post(
        "/mcp", MissingSessionHeaders,
        R"({"jsonrpc":"2.0","id":32,"method":"tools/list","params":{}})",
        "application/json");
    httplib::Headers ExpiredSessionHeaders = MissingSessionHeaders;
    ExpiredSessionHeaders.emplace("Mcp-Session-Id", "expired-session-id");
    const auto ExpiredSessionResult = Client.Post(
        "/mcp", ExpiredSessionHeaders,
        R"({"jsonrpc":"2.0","id":33,"method":"tools/list","params":{}})",
        "application/json");
    Runner.Expect(MissingSessionResult && MissingSessionResult->status == 400
            && ExpiredSessionResult && ExpiredSessionResult->status == 404,
        "Missing sessions return 400 while expired sessions request client reinitialization with 404");
    const auto ReinitializeResult = Client.Post(
        "/mcp", MissingSessionHeaders, InitializeBody.dump(), "application/json");
    Runner.Expect(ReinitializeResult && ReinitializeResult->status == 200
            && !ReinitializeResult->get_header_value("Mcp-Session-Id").empty()
            && ReinitializeResult->get_header_value("Mcp-Session-Id")
                != SessionId,
        "A client can establish a fresh session after an expired-session response");

    const FJson ToolBody = ModernRequest(11, "tools/call",
        {{"name", "echo"}, {"arguments", {{"over", "tcp"}}}});
    Headers.emplace("Mcp-Name", "echo");
    Headers.erase("Mcp-Method");
    Headers.emplace("Mcp-Method", "tools/call");
    const auto ToolResult = Client.Post(
        "/mcp", Headers, ToolBody.dump(), "application/json");
    Runner.Expect(ToolResult && ToolResult->status == 200
            && ToolResult->get_header_value("Content-Type").starts_with(
                "text/event-stream")
            && ToolResult->body.find("event: message") != std::string::npos
            && ToolResult->body.find("\"isError\":false") != std::string::npos,
        "Tool calls use request-scoped SSE and return the final JSON-RPC response");

    httplib::Client CancelClient("127.0.0.1", Config.Port);
    CancelClient.set_connection_timeout(2, 0);
    CancelClient.set_read_timeout(3, 0);
    httplib::Headers CancelHeaders = Headers;
    CancelHeaders.erase("Mcp-Name");
    CancelHeaders.emplace("Mcp-Name", "wait");
    const FJson WaitBody = ModernRequest(12, "tools/call",
        {{"name", "wait"}, {"arguments", FJson::object()}});
    std::thread CancelThread([&]
    {
        CancelClient.Post(
            "/mcp", CancelHeaders, WaitBody.dump(), "application/json");
    });
    std::this_thread::sleep_for(60ms);
    CancelClient.stop();
    CancelThread.join();
    const auto CancelDeadline = std::chrono::steady_clock::now() + 1s;
    while (!Provider.bCancellationObserved.load()
        && std::chrono::steady_clock::now() < CancelDeadline)
        std::this_thread::sleep_for(5ms);
    Runner.Expect(Provider.bCancellationObserved.load(),
        "Closing a tool-call SSE stream cancels the in-flight MCP operation");
    Server.Stop();
    Runner.Expect(!Server.IsRunning(), "The local MCP listener stops cleanly");
}
}

int main()
{
    FTestRunner Runner;
    TestBinding(Runner);
    TestRealLoopbackServer(Runner);
    return Runner.Finish();
}
