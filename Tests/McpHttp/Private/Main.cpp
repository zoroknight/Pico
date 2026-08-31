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
                R"({"type":"object","additionalProperties":false})", ""}};
    }

    Pico::FMcpToolCallResult CallTool(
        const Pico::FMcpToolCallContext&,
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
        if (Name != "echo") return {true, "unknown", ""};
        return {false, "echo", std::string(ArgumentsJson)};
    }

    int Calls = 0;
    std::atomic<bool> bCancellationObserved {false};
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

    const Pico::FMcpHttpResponse Unknown = Binding.Handle(Request(
        ModernRequest(4, "unknown/method"), "unknown/method"), "unknown");
    Runner.Expect(Unknown.Status == 404
            && FJson::parse(Unknown.Body)["error"]["code"] == -32601,
        "Unknown modern MCP methods map to HTTP 404 plus JSON-RPC MethodNotFound");

    const Pico::FMcpHttpServerStatus Status = Binding.GetStatus();
    Runner.Expect(Status.AcceptedRequests == 2
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
