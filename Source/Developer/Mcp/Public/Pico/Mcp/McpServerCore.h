#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
inline constexpr std::string_view McpProtocolModern = "2026-07-28";
inline constexpr std::string_view McpProtocolLegacy = "2025-11-25";

struct FMcpLimits
{
    std::size_t MaxRequestBytes = 1024 * 1024;
    std::size_t MaxJsonDepth = 32;
    std::size_t MaxStringBytes = 256 * 1024;
    std::size_t MaxLegacySessions = 8;
    std::size_t MaxInFlightRequestsPerPeer = 16;
    std::size_t MaxToolResultBytes = 1024 * 1024;
    std::chrono::milliseconds ToolTimeout {120000};
};

struct FMcpServerInfo
{
    std::string Name = "PicoMcpServer";
    std::string Version = "0.1.0";
    std::string Instructions =
        "Use the listed Pico tools through the protected editor execution pipeline.";
};

struct FMcpToolDescriptor
{
    std::string Name;
    std::string Title;
    std::string Description;
    std::string InputSchemaJson =
        R"({"type":"object","additionalProperties":false})";
    std::string OutputSchemaJson;
};

struct FMcpToolCallResult
{
    bool bIsError = false;
    std::string Text;
    std::string StructuredContentJson;
};

struct FMcpToolCallContext
{
    std::string PeerId;
    std::string RequestId;
};

class FMcpCancellationToken
{
public:
    FMcpCancellationToken();
    bool IsCancellationRequested() const;
    bool IsExpired() const;

private:
    struct FState;
    explicit FMcpCancellationToken(std::shared_ptr<FState> InState);
    std::shared_ptr<FState> State;
    friend class FMcpServerCore;
};

class IMcpToolProvider
{
public:
    virtual ~IMcpToolProvider() = default;
    virtual std::vector<FMcpToolDescriptor> ListTools() const = 0;
    virtual FMcpToolCallResult CallTool(
        const FMcpToolCallContext& Context,
        std::string_view Name,
        std::string_view ArgumentsJson,
        const FMcpCancellationToken& Cancellation) = 0;
};

struct FMcpRequestContext
{
    // A transport-supplied identity used only to correlate in-flight requests,
    // cancellation, and legacy sessions. Modern MCP semantics remain stateless.
    std::string PeerId;
};

struct FMcpMessageResult
{
    bool bHasResponse = false;
    std::string ResponseJson;
};

class FMcpServerCore
{
public:
    explicit FMcpServerCore(
        IMcpToolProvider& InToolProvider,
        FMcpServerInfo InServerInfo = {},
        FMcpLimits InLimits = {});
    ~FMcpServerCore();

    FMcpServerCore(const FMcpServerCore&) = delete;
    FMcpServerCore& operator=(const FMcpServerCore&) = delete;

    FMcpMessageResult HandleMessage(
        const FMcpRequestContext& Context,
        std::string_view MessageJson);
    void ClosePeer(std::string_view PeerId);
    void CancelPeerRequests(std::string_view PeerId);
    std::size_t GetLegacySessionCount() const;
    std::size_t GetInFlightRequestCount() const;

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
