#pragma once

#include "Pico/Mcp/McpServerCore.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Pico
{
struct FMcpHttpServerConfig
{
    std::string BindAddress = "127.0.0.1";
    std::uint16_t Port = 8765;
    std::string Endpoint = "/mcp";
    std::string BearerToken;
    std::size_t MaxRequestBytes = 1024 * 1024;
};

struct FMcpHttpRequest
{
    std::string Method;
    std::string Path;
    std::unordered_map<std::string, std::string> Headers;
    std::string Body;
};

struct FMcpHttpResponse
{
    int Status = 500;
    std::unordered_map<std::string, std::string> Headers;
    std::string Body;
};

struct FMcpHttpServerStatus
{
    bool bRunning = false;
    std::string BindAddress;
    std::uint16_t Port = 0;
    std::string Endpoint;
    std::uint64_t AcceptedRequests = 0;
    std::uint64_t RejectedRequests = 0;
    std::uint64_t ActiveRequests = 0;
    std::uint64_t ActiveSessions = 0;
    std::string LastError;
};

class FMcpHttpBinding
{
public:
    FMcpHttpBinding(FMcpServerCore& InCore, FMcpHttpServerConfig InConfig);

    FMcpHttpResponse Handle(
        const FMcpHttpRequest& Request,
        std::string_view PeerId);
    FMcpHttpServerStatus GetStatus() const;
    const FMcpHttpServerConfig& GetConfig() const;

private:
    std::optional<FMcpHttpResponse> Validate(
        const FMcpHttpRequest& Request);
    FMcpHttpResponse ExecuteValidated(
        const FMcpHttpRequest& Request,
        std::string_view PeerId);

    FMcpServerCore& Core;
    FMcpHttpServerConfig Config;
    std::atomic<std::uint64_t> AcceptedRequests {0};
    std::atomic<std::uint64_t> RejectedRequests {0};
    std::atomic<std::uint64_t> ActiveRequests {0};
    friend class FMcpHttpServer;
};

class FMcpHttpServer
{
public:
    explicit FMcpHttpServer(FMcpServerCore& InCore);
    ~FMcpHttpServer();

    FMcpHttpServer(const FMcpHttpServer&) = delete;
    FMcpHttpServer& operator=(const FMcpHttpServer&) = delete;

    bool Start(FMcpHttpServerConfig Config, std::string* OutError = nullptr);
    void Stop();
    bool IsRunning() const;
    FMcpHttpServerStatus GetStatus() const;

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};

std::string GenerateMcpBearerToken(std::size_t ByteCount = 32);
}
