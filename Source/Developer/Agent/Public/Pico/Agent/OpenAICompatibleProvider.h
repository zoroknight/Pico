#pragma once

#include "Pico/Agent/AgentProvider.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Pico
{
struct FAgentHttpRequest
{
    std::string Url;
    std::string AuthorizationBearer;
    std::string Body;
    std::uint32_t TimeoutMilliseconds = 30000;
};

struct FAgentHttpResponse
{
    bool bTransportSucceeded = false;
    int StatusCode = 0;
    std::string Body;
    std::uint32_t RetryAfterMilliseconds = 0;
    std::string Error;
};

class IAgentHttpTransport
{
public:
    virtual ~IAgentHttpTransport() = default;
    virtual FAgentHttpResponse PostJson(
        const FAgentHttpRequest& Request,
        const FCancellationToken* CancellationToken) = 0;
    virtual FAgentHttpResponse PostJsonStream(
        const FAgentHttpRequest& Request,
        const std::function<bool(std::string_view)>& OnChunk,
        const FCancellationToken* CancellationToken);
};

struct FOpenAICompatibleProviderSettings
{
    std::string Endpoint;
    std::string Model;
    std::string ApiKey;
    std::string SystemPrompt;
    std::string ToolCatalogJson = "[]";
    bool bSendThinkingSetting = false;
    bool bThinkingEnabled = false;
    std::uint32_t TimeoutMilliseconds = 30000;
    std::size_t MaxRetries = 2;
    std::uint32_t InitialRetryDelayMilliseconds = 500;
};

class FOpenAICompatibleProvider final : public IAgentProvider
{
public:
    FOpenAICompatibleProvider(
        FOpenAICompatibleProviderSettings Settings,
        std::shared_ptr<IAgentHttpTransport> Transport);
    ~FOpenAICompatibleProvider() override;

    FAgentProviderResponse Generate(
        const FAgentProviderRequest& Request,
        const FCancellationToken* CancellationToken) override;

    std::size_t GetRequestCount() const;
    std::size_t GetRetryCount() const;

private:
    bool BuildRequestBody(
        const FAgentProviderRequest& Request,
        std::string& OutBody,
        std::unordered_map<std::string, std::string>& OutApiToPicoToolNames,
        std::string& OutError) const;
    FAgentProviderResponse ParseResponse(
        const FAgentHttpResponse& Response,
        const std::unordered_map<std::string, std::string>& ApiToPicoToolNames) const;

    FOpenAICompatibleProviderSettings Settings;
    std::shared_ptr<IAgentHttpTransport> Transport;
    std::size_t RequestCount = 0;
    std::size_t RetryCount = 0;
};

std::shared_ptr<IAgentHttpTransport> CreatePlatformAgentHttpTransport();
}
