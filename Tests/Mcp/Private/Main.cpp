#include "TestRunner.h"

#include "Pico/Mcp/McpServerCore.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
using FJson = nlohmann::json;

class FFakeToolProvider final : public Pico::IMcpToolProvider
{
public:
    std::vector<Pico::FMcpToolDescriptor> ListTools() const override
    {
        return {
            {"world.describe", "Describe World", "Returns a world summary",
                R"({"type":"object","additionalProperties":false})",
                R"({"type":"object","properties":{"actors":{"type":"integer"}}})"},
            {"world.fail", "Fail", "Returns a domain failure"},
            {"world.wait", "Wait", "Waits until cancelled"}};
    }

    Pico::FMcpToolCallResult CallTool(
        const Pico::FMcpToolCallContext&,
        std::string_view Name,
        std::string_view ArgumentsJson,
        const Pico::FMcpCancellationToken& Cancellation) override
    {
        LastArguments = std::string(ArgumentsJson);
        if (Name == "world.fail")
            return {true, "World is not available", R"({"retryable":true})"};
        if (Name == "world.wait")
        {
            {
                std::lock_guard Lock(Mutex);
                bStarted = true;
            }
            Condition.notify_all();
            while (!Cancellation.IsCancellationRequested()
                && !Cancellation.IsExpired())
            {
                std::unique_lock Lock(Mutex);
                Condition.wait_for(Lock, 1ms);
            }
            bObservedCancellation = Cancellation.IsCancellationRequested();
            return {false, "late result", {}};
        }
        return {false, "World has 3 actors", R"({"actors":3})"};
    }

    void WaitUntilStarted()
    {
        std::unique_lock Lock(Mutex);
        Condition.wait_for(Lock, 1s, [this] { return bStarted; });
    }

    void ResetWait()
    {
        std::lock_guard Lock(Mutex);
        bStarted = false;
        bObservedCancellation = false;
    }

    std::string LastArguments;
    bool bStarted = false;
    bool bObservedCancellation = false;
    std::mutex Mutex;
    std::condition_variable Condition;
};

FJson ModernRequest(
    int Id,
    std::string Method,
    FJson Params = FJson::object(),
    std::string Version = std::string(Pico::McpProtocolModern))
{
    Params["_meta"] = {
        {"io.modelcontextprotocol/protocolVersion", std::move(Version)},
        {"io.modelcontextprotocol/clientCapabilities", FJson::object()},
        {"io.modelcontextprotocol/clientInfo",
            {{"name", "PicoMcpTests"}, {"version", "1"}}}};
    return {{"jsonrpc", "2.0"}, {"id", Id},
        {"method", std::move(Method)}, {"params", std::move(Params)}};
}

FJson Parse(const Pico::FMcpMessageResult& Result)
{
    return FJson::parse(Result.ResponseJson);
}

void TestModernProtocol(FTestRunner& Runner)
{
    FFakeToolProvider Provider;
    Pico::FMcpServerCore Core(Provider);
    const Pico::FMcpRequestContext Context {"modern-peer"};

    const FJson Discover = Parse(Core.HandleMessage(
        Context, ModernRequest(1, "server/discover").dump()));
    Runner.Expect(Discover["result"]["supportedVersions"][0].get<std::string>()
            == Pico::McpProtocolModern
            && Discover["result"]["resultType"] == "complete",
        "Modern server/discover reports the current protocol and complete result type");

    const FJson List = Parse(Core.HandleMessage(
        Context, ModernRequest(2, "tools/list").dump()));
    Runner.Expect(List["result"]["tools"].size() == 3
            && List["result"]["tools"][0]["name"] == "world.describe"
            && List["result"]["ttlMs"] == 1000
            && List["result"]["cacheScope"] == "private"
            && List["result"].contains("_meta"),
        "tools/list is deterministic and includes modern cache and server metadata");

    const FJson Call = Parse(Core.HandleMessage(Context,
        ModernRequest(3, "tools/call",
            {{"name", "world.describe"},
                {"arguments", {{"detail", true}}}}).dump()));
    Runner.Expect(Call["result"]["isError"] == false
            && Call["result"]["structuredContent"]["actors"] == 3
            && Provider.LastArguments.find("detail") != std::string::npos,
        "tools/call forwards object arguments and preserves structured content");

    const FJson DomainFailure = Parse(Core.HandleMessage(Context,
        ModernRequest(4, "tools/call",
            {{"name", "world.fail"}, {"arguments", FJson::object()}}).dump()));
    Runner.Expect(DomainFailure["result"]["isError"] == true
            && !DomainFailure.contains("error"),
        "Tool domain failures remain MCP results instead of protocol failures");

    const FJson UnknownTool = Parse(Core.HandleMessage(Context,
        ModernRequest(5, "tools/call",
            {{"name", "missing"}, {"arguments", FJson::object()}}).dump()));
    Runner.Expect(UnknownTool["error"]["code"] == -32602,
        "Unknown tools map to Invalid params");

    const FJson UnknownMethod = Parse(Core.HandleMessage(
        Context, ModernRequest(6, "unknown/method").dump()));
    Runner.Expect(UnknownMethod["error"]["code"] == -32601,
        "Unknown methods map to Method not found");

    FJson MissingCapabilities = ModernRequest(7, "ping");
    MissingCapabilities["params"]["_meta"].erase(
        "io.modelcontextprotocol/clientCapabilities");
    const FJson Missing = Parse(Core.HandleMessage(
        Context, MissingCapabilities.dump()));
    Runner.Expect(Missing["error"]["code"] == -32602,
        "Modern requests require per-request client capabilities");

    const FJson Unsupported = Parse(Core.HandleMessage(Context,
        ModernRequest(8, "ping", FJson::object(), "1900-01-01").dump()));
    Runner.Expect(Unsupported["error"]["code"] == -32022
            && Unsupported["error"]["data"]["supported"].size() == 2,
        "Unsupported versions return the modern negotiation error and supported eras");
}

void TestLegacyCompatibility(FTestRunner& Runner)
{
    FFakeToolProvider Provider;
    Pico::FMcpLimits Limits;
    Limits.MaxLegacySessions = 1;
    Pico::FMcpServerCore Core(Provider, {}, Limits);
    const Pico::FMcpRequestContext First {"legacy-one"};

    const FJson BeforeInit = Parse(Core.HandleMessage(First,
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})"));
    Runner.Expect(BeforeInit["error"]["code"] == -32600,
        "Legacy tool requests are rejected before initialization");

    const FJson Ping = Parse(Core.HandleMessage(First,
        R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
    Runner.Expect(Ping["result"].is_object(),
        "Legacy ping remains available before initialization");

    const FJson Initialize = Parse(Core.HandleMessage(First,
        R"({"jsonrpc":"2.0","id":3,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
    Runner.Expect(Initialize["result"]["protocolVersion"].get<std::string>()
            == Pico::McpProtocolLegacy && Core.GetLegacySessionCount() == 1,
        "Legacy initialize creates a bounded compatibility session");

    Core.HandleMessage(First,
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    const FJson List = Parse(Core.HandleMessage(First,
        R"({"jsonrpc":"2.0","id":4,"method":"tools/list","params":{}})"));
    Runner.Expect(List["result"]["tools"].size() == 3
            && !List["result"].contains("resultType"),
        "Legacy initialized peers can list tools with legacy response shape");

    const FJson Duplicate = Parse(Core.HandleMessage(First,
        R"({"jsonrpc":"2.0","id":5,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})"));
    Runner.Expect(Duplicate["error"]["code"] == -32600,
        "Duplicate legacy initialization fails closed");

    const FJson Limit = Parse(Core.HandleMessage({"legacy-two"},
        R"({"jsonrpc":"2.0","id":6,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})"));
    Runner.Expect(Limit["error"]["message"] == "Legacy session limit reached",
        "Legacy compatibility sessions obey the configured limit");

    Core.ClosePeer(First.PeerId);
    Runner.Expect(Core.GetLegacySessionCount() == 0,
        "Closing a peer removes its legacy session state");
}

void TestProtocolFailuresAndLimits(FTestRunner& Runner)
{
    FFakeToolProvider Provider;
    Pico::FMcpLimits Limits;
    Limits.MaxRequestBytes = 512;
    Limits.MaxJsonDepth = 8;
    Limits.MaxStringBytes = 16;
    Limits.MaxToolResultBytes = 8;
    Pico::FMcpServerCore Core(Provider, {}, Limits);
    const Pico::FMcpRequestContext Context {"limits"};

    Runner.Expect(Parse(Core.HandleMessage(Context, "{"))["error"]["code"]
            == -32700,
        "Malformed JSON maps to Parse error");
    Runner.Expect(Parse(Core.HandleMessage(Context, "[]"))["error"]["code"]
            == -32600,
        "JSON-RPC batches are rejected explicitly");
    Runner.Expect(Parse(Core.HandleMessage(Context,
        R"({"jsonrpc":2,"id":1,"method":"ping"})"))["error"]["code"]
            == -32600,
        "Wrongly typed JSON-RPC fields fail without escaping parser exceptions");
    Runner.Expect(Parse(Core.HandleMessage(Context,
        R"({"jsonrpc":"2.0","id":null,"method":"ping"})"))["error"]["code"]
            == -32600,
        "Null request ids are rejected");

    const std::string Oversize(513, 'x');
    Runner.Expect(Parse(Core.HandleMessage(Context, Oversize))["error"]["message"]
            == "Request exceeds size limit",
        "Request byte limits apply before parsing");

    FJson LongString = ModernRequest(1, "ping");
    LongString["params"]["value"] = std::string(17, 'x');
    Runner.Expect(Parse(Core.HandleMessage(Context, LongString.dump()))
            ["error"]["message"] == "Invalid or over-limit request",
        "Nested string limits are enforced");

    FJson DeepValue = FJson::object();
    FJson* Cursor = &DeepValue;
    for (int Index = 0; Index < 10; ++Index)
        Cursor = &((*Cursor)["next"] = FJson::object());
    FJson DeepRequest = ModernRequest(11, "ping");
    DeepRequest["params"]["value"] = std::move(DeepValue);
    Runner.Expect(Parse(Core.HandleMessage(Context, DeepRequest.dump()))
            ["error"]["message"] == "Invalid or over-limit request",
        "Nested JSON depth limits are enforced");

    const FJson ResultLimit = Parse(Core.HandleMessage(Context,
        ModernRequest(2, "tools/call",
            {{"name", "world.describe"}, {"arguments", FJson::object()}}).dump()));
    Runner.Expect(ResultLimit["result"]["isError"] == true
            && ResultLimit["result"]["content"][0]["text"]
                == "Tool result exceeds size limit",
        "Oversized tool output becomes a bounded tool execution error");
}

void TestCancellationAndDuplicateIds(FTestRunner& Runner)
{
    FFakeToolProvider Provider;
    Pico::FMcpServerCore Core(Provider);
    const Pico::FMcpRequestContext Context {"async-peer"};
    const std::string WaitRequest = ModernRequest(41, "tools/call",
        {{"name", "world.wait"}, {"arguments", FJson::object()}}).dump();

    Pico::FMcpMessageResult WaitResult;
    std::thread Worker([&] { WaitResult = Core.HandleMessage(Context, WaitRequest); });
    Provider.WaitUntilStarted();
    Runner.Expect(Core.GetInFlightRequestCount() == 1,
        "Long-running tool calls are tracked independently of transport");

    const FJson OutOfOrder = Parse(Core.HandleMessage(Context,
        ModernRequest(42, "tools/call",
            {{"name", "world.describe"}, {"arguments", FJson::object()}}).dump()));
    Runner.Expect(OutOfOrder["result"]["isError"] == false
            && Core.GetInFlightRequestCount() == 1,
        "A later independent request can complete while an earlier call is pending");

    const FJson Duplicate = Parse(Core.HandleMessage(Context, WaitRequest));
    Runner.Expect(Duplicate["error"]["message"] == "Duplicate in-flight request id",
        "Duplicate in-flight request ids fail without starting a second side effect");

    Core.HandleMessage(Context,
        R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":41,"reason":"test"}})");
    Worker.join();
    Runner.Expect(Provider.bObservedCancellation && !WaitResult.bHasResponse
            && Core.GetInFlightRequestCount() == 0,
        "Cancellation reaches the provider and suppresses a late response");

    Provider.ResetWait();
    std::thread ClosingWorker(
        [&] { WaitResult = Core.HandleMessage(Context, WaitRequest); });
    Provider.WaitUntilStarted();
    Core.ClosePeer(Context.PeerId);
    ClosingWorker.join();
    Runner.Expect(Provider.bObservedCancellation && !WaitResult.bHasResponse,
        "Closing a transport peer cooperatively cancels its in-flight calls");
}

void TestToolTimeout(FTestRunner& Runner)
{
    FFakeToolProvider Provider;
    Pico::FMcpLimits Limits;
    Limits.ToolTimeout = 5ms;
    Pico::FMcpServerCore Core(Provider, {}, Limits);
    const Pico::FMcpRequestContext Context {"timeout-peer"};
    const FJson Result = Parse(Core.HandleMessage(Context,
        ModernRequest(51, "tools/call",
            {{"name", "world.wait"}, {"arguments", FJson::object()}}).dump()));
    Runner.Expect(Result["result"]["isError"] == true
            && Result["result"]["content"][0]["text"] == "Tool call timed out"
            && Core.GetInFlightRequestCount() == 0,
        "Tool deadlines are visible to providers and produce a bounded tool error");
}
}

int main()
{
    FTestRunner Runner;
    TestModernProtocol(Runner);
    TestLegacyCompatibility(Runner);
    TestProtocolFailuresAndLimits(Runner);
    TestCancellationAndDuplicateIds(Runner);
    TestToolTimeout(Runner);
    return Runner.Finish();
}
