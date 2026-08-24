#include "Pico/Agent/OpenAICompatibleProvider.h"

#include "Pico/Tasks/TaskSystem.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <limits>
#include <vector>
#endif

namespace Pico
{
#ifdef _WIN32
namespace
{
std::wstring ToWide(std::string_view Text)
{
    if (Text.empty()) return {};
    const int Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        Text.data(), static_cast<int>(Text.size()), nullptr, 0);
    if (Count <= 0) return {};
    std::wstring Result(static_cast<std::size_t>(Count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(),
        static_cast<int>(Text.size()), Result.data(), Count);
    return Result;
}

std::string FromWide(std::wstring_view Text)
{
    if (Text.empty()) return {};
    const int Count = WideCharToMultiByte(CP_UTF8, 0, Text.data(),
        static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
    std::string Result(static_cast<std::size_t>(Count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()),
        Result.data(), Count, nullptr, nullptr);
    return Result;
}

std::string WinHttpError(std::string_view Prefix)
{
    return std::string(Prefix) + " (WinHTTP " + std::to_string(GetLastError()) + ")";
}

class FWinHttpAgentTransport final : public IAgentHttpTransport
{
public:
    FAgentHttpResponse PostJson(
        const FAgentHttpRequest& Request,
        const FCancellationToken* CancellationToken) override
    {
        return Post(Request, {}, CancellationToken);
    }

    FAgentHttpResponse PostJsonStream(
        const FAgentHttpRequest& Request,
        const std::function<bool(std::string_view)>& OnChunk,
        const FCancellationToken* CancellationToken) override
    {
        return Post(Request, OnChunk, CancellationToken);
    }

private:
    FAgentHttpResponse Post(
        const FAgentHttpRequest& Request,
        const std::function<bool(std::string_view)>& OnChunk,
        const FCancellationToken* CancellationToken)
    {
        if (CancellationToken && CancellationToken->IsCancellationRequested())
            return {false, 0, {}, 0, "Cancelled"};
        const std::wstring Url = ToWide(Request.Url);
        URL_COMPONENTS Components {};
        Components.dwStructSize = sizeof(Components);
        Components.dwSchemeLength = static_cast<DWORD>(-1);
        Components.dwHostNameLength = static_cast<DWORD>(-1);
        Components.dwUrlPathLength = static_cast<DWORD>(-1);
        Components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (Url.empty() || !WinHttpCrackUrl(Url.c_str(), 0, 0, &Components))
            return {false, 0, {}, 0, "Provider URL is invalid"};

        const std::wstring Host(Components.lpszHostName, Components.dwHostNameLength);
        std::wstring Path(Components.lpszUrlPath, Components.dwUrlPathLength);
        if (Components.dwExtraInfoLength > 0)
            Path.append(Components.lpszExtraInfo, Components.dwExtraInfoLength);
        HINTERNET Session = WinHttpOpen(L"PicoAgent/0.1",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!Session) return {false, 0, {}, 0, WinHttpError("WinHttpOpen failed")};
        const int Timeout = static_cast<int>(std::min<std::uint32_t>(
            Request.TimeoutMilliseconds, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        WinHttpSetTimeouts(Session, Timeout, Timeout, Timeout, Timeout);
        HINTERNET Connection = WinHttpConnect(Session, Host.c_str(), Components.nPort, 0);
        if (!Connection)
        {
            const std::string Error = WinHttpError("WinHttpConnect failed");
            WinHttpCloseHandle(Session);
            return {false, 0, {}, 0, Error};
        }
        const DWORD Flags = Components.nScheme == INTERNET_SCHEME_HTTPS
            ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET HttpRequest = WinHttpOpenRequest(Connection, L"POST", Path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Flags);
        if (!HttpRequest)
        {
            const std::string Error = WinHttpError("WinHttpOpenRequest failed");
            WinHttpCloseHandle(Connection);
            WinHttpCloseHandle(Session);
            return {false, 0, {}, 0, Error};
        }
        const std::wstring Headers = L"Content-Type: application/json\r\nAuthorization: Bearer "
            + ToWide(Request.AuthorizationBearer) + L"\r\n";
        if (Request.Body.size() > std::numeric_limits<DWORD>::max())
        {
            WinHttpCloseHandle(HttpRequest);
            WinHttpCloseHandle(Connection);
            WinHttpCloseHandle(Session);
            return {false, 0, {}, 0, "Provider request body is too large"};
        }
        const BOOL bSent = WinHttpSendRequest(HttpRequest, Headers.c_str(),
            static_cast<DWORD>(-1), const_cast<char*>(Request.Body.data()),
            static_cast<DWORD>(Request.Body.size()), static_cast<DWORD>(Request.Body.size()), 0);
        if (!bSent || !WinHttpReceiveResponse(HttpRequest, nullptr))
        {
            const std::string Error = WinHttpError("Provider request failed");
            WinHttpCloseHandle(HttpRequest);
            WinHttpCloseHandle(Connection);
            WinHttpCloseHandle(Session);
            return {false, 0, {}, 0, Error};
        }

        DWORD StatusCode = 0;
        DWORD StatusSize = sizeof(StatusCode);
        WinHttpQueryHeaders(HttpRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusSize, WINHTTP_NO_HEADER_INDEX);
        std::string Body;
        while (true)
        {
            if (CancellationToken && CancellationToken->IsCancellationRequested())
            {
                WinHttpCloseHandle(HttpRequest);
                WinHttpCloseHandle(Connection);
                WinHttpCloseHandle(Session);
                return {false, 0, {}, 0, "Cancelled"};
            }
            DWORD Available = 0;
            if (!WinHttpQueryDataAvailable(HttpRequest, &Available))
            {
                const std::string Error = WinHttpError("Could not read provider response");
                WinHttpCloseHandle(HttpRequest);
                WinHttpCloseHandle(Connection);
                WinHttpCloseHandle(Session);
                return {false, 0, {}, 0, Error};
            }
            if (Available == 0) break;
            std::vector<char> Buffer(Available);
            DWORD Read = 0;
            if (!WinHttpReadData(HttpRequest, Buffer.data(), Available, &Read))
            {
                const std::string Error = WinHttpError("Could not read provider response");
                WinHttpCloseHandle(HttpRequest);
                WinHttpCloseHandle(Connection);
                WinHttpCloseHandle(Session);
                return {false, 0, {}, 0, Error};
            }
            if (OnChunk && StatusCode >= 200 && StatusCode < 300)
            {
                if (!OnChunk(std::string_view(Buffer.data(), Read)))
                {
                    WinHttpCloseHandle(HttpRequest);
                    WinHttpCloseHandle(Connection);
                    WinHttpCloseHandle(Session);
                    return {false, 0, {}, 0, "Provider stream parser rejected a response event"};
                }
            }
            else
            {
                Body.append(Buffer.data(), Read);
            }
        }

        std::uint32_t RetryAfterMilliseconds = 0;
        wchar_t RetryAfter[64] {};
        DWORD RetrySize = sizeof(RetryAfter);
        if (WinHttpQueryHeaders(HttpRequest, WINHTTP_QUERY_CUSTOM,
            L"Retry-After", RetryAfter, &RetrySize, WINHTTP_NO_HEADER_INDEX))
        {
            try
            {
                RetryAfterMilliseconds = static_cast<std::uint32_t>(
                    std::stoul(FromWide(RetryAfter)) * 1000UL);
            }
            catch (...) {}
        }
        WinHttpCloseHandle(HttpRequest);
        WinHttpCloseHandle(Connection);
        WinHttpCloseHandle(Session);
        return {true, static_cast<int>(StatusCode), std::move(Body),
            RetryAfterMilliseconds, {}};
    }
};
}
#endif

std::shared_ptr<IAgentHttpTransport> CreatePlatformAgentHttpTransport()
{
#ifdef _WIN32
    return std::make_shared<FWinHttpAgentTransport>();
#else
    return {};
#endif
}
}
