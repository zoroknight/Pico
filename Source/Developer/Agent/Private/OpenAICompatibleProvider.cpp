#include "Pico/Agent/OpenAICompatibleProvider.h"

#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

bool IsCancelled(const FCancellationToken* Token)
{
    return Token && Token->IsCancellationRequested();
}

bool WaitCancelable(std::uint32_t Milliseconds, const FCancellationToken* Token)
{
    std::uint32_t Remaining = Milliseconds;
    while (Remaining > 0)
    {
        if (IsCancelled(Token)) return false;
        const std::uint32_t Slice = std::min<std::uint32_t>(Remaining, 25);
        std::this_thread::sleep_for(std::chrono::milliseconds(Slice));
        Remaining -= Slice;
    }
    return !IsCancelled(Token);
}

void ClearSecret(std::string& Text)
{
    volatile char* Data = Text.empty() ? nullptr : Text.data();
    for (std::size_t Index = 0; Index < Text.size(); ++Index)
        Data[Index] = 0;
    Text.clear();
}

std::string RoleName(EAgentRole Role)
{
    switch (Role)
    {
    case EAgentRole::System: return "system";
    case EAgentRole::User: return "user";
    case EAgentRole::Assistant: return "assistant";
    case EAgentRole::Tool: return "tool";
    }
    return "user";
}

std::uint64_t StableHash(std::string_view Text)
{
    std::uint64_t Hash = 1469598103934665603ULL;
    for (const unsigned char Character : Text)
    {
        Hash ^= Character;
        Hash *= 1099511628211ULL;
    }
    return Hash;
}

std::string MakeApiToolName(std::string_view PicoName)
{
    std::string Result;
    Result.reserve(std::min<std::size_t>(PicoName.size(), 64));
    for (const unsigned char Character : PicoName)
    {
        Result.push_back(std::isalnum(Character) || Character == '_' || Character == '-'
            ? static_cast<char>(Character) : '_');
        if (Result.size() == 64) break;
    }
    return Result;
}

std::string AddHashSuffix(std::string Name, std::string_view Original)
{
    std::ostringstream Suffix;
    Suffix << '_' << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<std::uint32_t>(StableHash(Original));
    const std::string SuffixText = Suffix.str();
    if (Name.size() + SuffixText.size() > 64)
        Name.resize(64 - SuffixText.size());
    return Name + SuffixText;
}
}

FOpenAICompatibleProvider::FOpenAICompatibleProvider(
    FOpenAICompatibleProviderSettings InSettings,
    std::shared_ptr<IAgentHttpTransport> InTransport)
    : Settings(std::move(InSettings))
    , Transport(std::move(InTransport))
{
}

FOpenAICompatibleProvider::~FOpenAICompatibleProvider()
{
    ClearSecret(Settings.ApiKey);
}

FAgentProviderResponse FOpenAICompatibleProvider::Generate(
    const FAgentProviderRequest& Request,
    const FCancellationToken* CancellationToken)
{
    if (IsCancelled(CancellationToken))
        return {false, false, {}, "Cancelled", {}};
    if (!Transport || Settings.Endpoint.empty() || Settings.Model.empty()
        || Settings.ApiKey.empty())
    {
        return {false, false, {},
            "Provider endpoint, model, API key, and transport are required", {}};
    }

    std::string Body;
    std::string Error;
    std::unordered_map<std::string, std::string> ApiToPicoToolNames;
    if (!BuildRequestBody(Request, Body, ApiToPicoToolNames, Error))
        return {false, false, {}, std::move(Error), {}};

    std::uint32_t RetryDelay = std::max<std::uint32_t>(
        Settings.InitialRetryDelayMilliseconds, 1);
    for (std::size_t Attempt = 0; Attempt <= Settings.MaxRetries; ++Attempt)
    {
        ++RequestCount;
        FAgentHttpRequest HttpRequest;
        HttpRequest.Url = Settings.Endpoint;
        HttpRequest.AuthorizationBearer = Settings.ApiKey;
        HttpRequest.Body = Body;
        HttpRequest.TimeoutMilliseconds = Settings.TimeoutMilliseconds;
        const FAgentHttpResponse HttpResponse =
            Transport->PostJson(HttpRequest, CancellationToken);
        ClearSecret(HttpRequest.AuthorizationBearer);
        if (IsCancelled(CancellationToken) || HttpResponse.Error == "Cancelled")
            return {false, false, {}, "Cancelled", {}};

        const bool bTransient = !HttpResponse.bTransportSucceeded
            || HttpResponse.StatusCode == 408 || HttpResponse.StatusCode == 429
            || HttpResponse.StatusCode >= 500;
        if (HttpResponse.bTransportSucceeded
            && HttpResponse.StatusCode >= 200 && HttpResponse.StatusCode < 300)
        {
            return ParseResponse(HttpResponse, ApiToPicoToolNames);
        }
        if (!bTransient || Attempt == Settings.MaxRetries)
        {
            return ParseResponse(HttpResponse, ApiToPicoToolNames);
        }
        ++RetryCount;
        const std::uint32_t Delay = HttpResponse.RetryAfterMilliseconds > 0
            ? std::min<std::uint32_t>(HttpResponse.RetryAfterMilliseconds, 10000)
            : RetryDelay;
        if (!WaitCancelable(Delay, CancellationToken))
            return {false, false, {}, "Cancelled", {}};
        RetryDelay = std::min<std::uint32_t>(RetryDelay * 2, 10000);
    }
    return {false, false, {}, "Provider retry loop ended unexpectedly", {}};
}

std::size_t FOpenAICompatibleProvider::GetRequestCount() const { return RequestCount; }
std::size_t FOpenAICompatibleProvider::GetRetryCount() const { return RetryCount; }

bool FOpenAICompatibleProvider::BuildRequestBody(
    const FAgentProviderRequest& Request,
    std::string& OutBody,
    std::unordered_map<std::string, std::string>& OutApiToPicoToolNames,
    std::string& OutError) const
{
    try
    {
        FJson Body;
        Body["model"] = Settings.Model;
        Body["stream"] = false;
        Body["temperature"] = 0.2;
        if (Settings.bSendThinkingSetting)
        {
            Body["thinking"] = {
                {"type", Settings.bThinkingEnabled ? "enabled" : "disabled"}
            };
        }
        Body["messages"] = FJson::array();
        if (!Settings.SystemPrompt.empty())
        {
            Body["messages"].push_back(
                {{"role", "system"}, {"content", Settings.SystemPrompt}});
        }

        std::unordered_map<std::string, std::string> PicoToApiToolNames;
        std::unordered_set<std::string> UsedApiNames;
        FJson Catalog = FJson::parse(Settings.ToolCatalogJson);
        FJson Tools = FJson::array();
        for (const FJson& Entry : Catalog)
        {
            const std::string PicoName = Entry.at("name").get<std::string>();
            std::string ApiName = MakeApiToolName(PicoName);
            if (ApiName.empty() || UsedApiNames.contains(ApiName))
                ApiName = AddHashSuffix(ApiName.empty() ? "pico_tool" : ApiName, PicoName);
            UsedApiNames.insert(ApiName);
            PicoToApiToolNames[PicoName] = ApiName;
            OutApiToPicoToolNames[ApiName] = PicoName;
            Tools.push_back({{"type", "function"}, {"function", {
                {"name", ApiName}, {"description", Entry.value("description", "")},
                {"parameters", Entry.at("input_schema")}}}});
        }

        for (const FAgentMessage& Message : Request.Messages)
        {
            FJson MessageJson {{"role", RoleName(Message.Role)}};
            if (Message.Role == EAgentRole::Tool)
            {
                MessageJson["content"] = Message.Content;
                MessageJson["tool_call_id"] = Message.ToolCallId;
            }
            else
            {
                MessageJson["content"] = Message.Content;
                if (Message.Role == EAgentRole::Assistant && !Message.ToolCalls.empty())
                {
                    MessageJson["tool_calls"] = FJson::array();
                    for (const FAgentToolCall& Call : Message.ToolCalls)
                    {
                        const auto NameIt = PicoToApiToolNames.find(Call.Name);
                        const std::string ApiName = NameIt != PicoToApiToolNames.end()
                            ? NameIt->second : MakeApiToolName(Call.Name);
                        MessageJson["tool_calls"].push_back({{"id", Call.Id},
                            {"type", "function"}, {"function", {
                                {"name", ApiName}, {"arguments", Call.ArgumentsJson}}}});
                    }
                }
            }
            Body["messages"].push_back(std::move(MessageJson));
        }
        if (!Tools.empty())
        {
            Body["tools"] = std::move(Tools);
            Body["tool_choice"] = "auto";
        }
        OutBody = Body.dump();
        return true;
    }
    catch (const std::exception& Exception)
    {
        OutError = "Could not build provider request: " + std::string(Exception.what());
        return false;
    }
}

FAgentProviderResponse FOpenAICompatibleProvider::ParseResponse(
    const FAgentHttpResponse& Response,
    const std::unordered_map<std::string, std::string>& ApiToPicoToolNames) const
{
    if (!Response.bTransportSucceeded)
        return {false, false, {}, Response.Error.empty() ? "HTTP transport failed" : Response.Error, {}};
    if (Response.StatusCode < 200 || Response.StatusCode >= 300)
    {
        std::string Message = "Provider returned HTTP " + std::to_string(Response.StatusCode);
        try
        {
            const FJson ErrorJson = FJson::parse(Response.Body);
            if (ErrorJson.contains("error"))
                Message += ": " + ErrorJson["error"].value("message", Response.Body);
        }
        catch (...) { if (!Response.Body.empty()) Message += ": " + Response.Body; }
        return {false, false, {}, std::move(Message), {}};
    }
    try
    {
        const FJson Root = FJson::parse(Response.Body);
        const FJson& Choice = Root.at("choices").at(0);
        const FJson& Message = Choice.at("message");
        FAgentProviderResponse Result;
        if (Message.contains("content") && Message["content"].is_string())
            Result.Content = Message["content"].get<std::string>();
        if (Message.contains("tool_calls") && Message["tool_calls"].is_array())
        {
            for (const FJson& ToolCall : Message["tool_calls"])
            {
                const std::string ApiName = ToolCall.at("function").at("name").get<std::string>();
                const auto NameIt = ApiToPicoToolNames.find(ApiName);
                Result.ToolCalls.push_back({ToolCall.at("id").get<std::string>(),
                    NameIt != ApiToPicoToolNames.end() ? NameIt->second : ApiName,
                    ToolCall.at("function").at("arguments").get<std::string>()});
            }
        }
        const std::string FinishReason = Choice.value("finish_reason", "");
        Result.bFinal = Result.ToolCalls.empty()
            && (FinishReason == "stop" || FinishReason.empty());
        if (!Result.bFinal && Result.ToolCalls.empty())
        {
            Result.bSucceeded = false;
            Result.Error = "Provider stopped without a final answer or tool call: " + FinishReason;
        }
        return Result;
    }
    catch (const std::exception& Exception)
    {
        return {false, false, {},
            "Provider response was invalid: " + std::string(Exception.what()), {}};
    }
}
}
