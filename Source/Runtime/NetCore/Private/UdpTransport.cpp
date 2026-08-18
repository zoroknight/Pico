#include "Pico/Net/UdpTransport.h"

#include "Pico/Core/Platform.h"

#include <array>
#include <format>
#include <limits>

#if PICO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

namespace Pico
{
namespace
{
#if PICO_PLATFORM_WINDOWS
SOCKET ToNativeSocket(std::uintptr_t Value)
{
    return static_cast<SOCKET>(Value);
}

bool ResolveIPv4(const FNetAddress& Address, sockaddr_in& OutAddress)
{
    OutAddress = {};
    OutAddress.sin_family = AF_INET;
    OutAddress.sin_port = htons(Address.Port);
    if (Address.Host == "0.0.0.0" || Address.Host == "*")
    {
        OutAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }

    addrinfo Hints {};
    Hints.ai_family = AF_INET;
    Hints.ai_socktype = SOCK_DGRAM;
    addrinfo* Result = nullptr;
    if (getaddrinfo(Address.Host.c_str(), nullptr, &Hints, &Result) != 0
        || Result == nullptr)
    {
        if (Result != nullptr) freeaddrinfo(Result);
        return false;
    }
    OutAddress.sin_addr =
        reinterpret_cast<sockaddr_in*>(Result->ai_addr)->sin_addr;
    freeaddrinfo(Result);
    return true;
}
#endif
}

FUdpTransport::FUdpTransport() = default;

FUdpTransport::~FUdpTransport()
{
    Close();
}

bool FUdpTransport::Open(const FNetAddress& InLocalAddress)
{
    Close();
#if PICO_PLATFORM_WINDOWS
    WSADATA Data {};
    if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
    {
        LastError = "WSAStartup failed";
        return false;
    }
    bPlatformInitialized = true;

    const SOCKET NativeSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (NativeSocket == INVALID_SOCKET)
    {
        SetSocketError("socket", WSAGetLastError());
        Close();
        return false;
    }
    Socket = static_cast<std::uintptr_t>(NativeSocket);

    // Windows otherwise converts an ICMP Port Unreachable response into a
    // WSAECONNRESET on a later recvfrom call. UDP handshakes must be free to
    // retry while a separately launched server is still binding its port.
    BOOL bReportUdpConnectionReset = FALSE;
    DWORD ControlBytes = 0;
    WSAIoctl(
        NativeSocket,
        SIO_UDP_CONNRESET,
        &bReportUdpConnectionReset,
        sizeof(bReportUdpConnectionReset),
        nullptr,
        0,
        &ControlBytes,
        nullptr,
        nullptr);

    u_long NonBlocking = 1;
    if (ioctlsocket(NativeSocket, FIONBIO, &NonBlocking) != 0)
    {
        SetSocketError("ioctlsocket", WSAGetLastError());
        Close();
        return false;
    }

    sockaddr_in BindAddress {};
    if (!ResolveIPv4(InLocalAddress, BindAddress)
        || bind(NativeSocket, reinterpret_cast<sockaddr*>(&BindAddress),
            sizeof(BindAddress)) != 0)
    {
        SetSocketError("bind", WSAGetLastError());
        Close();
        return false;
    }

    sockaddr_in ActualAddress {};
    int ActualLength = sizeof(ActualAddress);
    if (getsockname(NativeSocket, reinterpret_cast<sockaddr*>(&ActualAddress),
            &ActualLength) != 0)
    {
        SetSocketError("getsockname", WSAGetLastError());
        Close();
        return false;
    }
    std::array<char, INET_ADDRSTRLEN> Host {};
    inet_ntop(AF_INET, &ActualAddress.sin_addr, Host.data(),
        static_cast<DWORD>(Host.size()));
    LocalAddress.Host = Host.data();
    LocalAddress.Port = ntohs(ActualAddress.sin_port);
    LastError.clear();
    return true;
#else
    (void)InLocalAddress;
    LastError = "UDP transport is not implemented for this platform";
    return false;
#endif
}

void FUdpTransport::Close()
{
#if PICO_PLATFORM_WINDOWS
    if (ToNativeSocket(Socket) != INVALID_SOCKET)
    {
        closesocket(ToNativeSocket(Socket));
    }
    if (bPlatformInitialized) WSACleanup();
#endif
    Socket = static_cast<std::uintptr_t>(-1);
    LocalAddress = {};
    bPlatformInitialized = false;
}

bool FUdpTransport::IsOpen() const
{
#if PICO_PLATFORM_WINDOWS
    return ToNativeSocket(Socket) != INVALID_SOCKET;
#else
    return false;
#endif
}

bool FUdpTransport::SendTo(
    const FNetAddress& RemoteAddress,
    std::span<const uint8> Bytes)
{
#if PICO_PLATFORM_WINDOWS
    if (!IsOpen() || Bytes.empty() || Bytes.size() > MaxNetDatagramBytes
        || Bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        LastError = "invalid UDP send";
        return false;
    }
    sockaddr_in NativeAddress {};
    if (!ResolveIPv4(RemoteAddress, NativeAddress))
    {
        LastError = "could not resolve UDP destination";
        return false;
    }
    const int Sent = sendto(
        ToNativeSocket(Socket),
        reinterpret_cast<const char*>(Bytes.data()),
        static_cast<int>(Bytes.size()), 0,
        reinterpret_cast<const sockaddr*>(&NativeAddress),
        sizeof(NativeAddress));
    if (Sent != static_cast<int>(Bytes.size()))
    {
        SetSocketError("sendto", WSAGetLastError());
        return false;
    }
    return true;
#else
    (void)RemoteAddress;
    (void)Bytes;
    return false;
#endif
}

ENetReceiveResult FUdpTransport::ReceiveFrom(
    FNetAddress& OutRemoteAddress,
    std::vector<uint8>& OutBytes)
{
    OutRemoteAddress = {};
    OutBytes.clear();
#if PICO_PLATFORM_WINDOWS
    if (!IsOpen())
    {
        LastError = "UDP transport is closed";
        return ENetReceiveResult::Error;
    }
    std::array<uint8, MaxNetDatagramBytes + 1> Buffer {};
    sockaddr_in RemoteAddress {};
    int RemoteLength = sizeof(RemoteAddress);
    const int Received = recvfrom(
        ToNativeSocket(Socket), reinterpret_cast<char*>(Buffer.data()),
        static_cast<int>(Buffer.size()), 0,
        reinterpret_cast<sockaddr*>(&RemoteAddress), &RemoteLength);
    if (Received == SOCKET_ERROR)
    {
        const int ErrorCode = WSAGetLastError();
        if (ErrorCode == WSAEWOULDBLOCK) return ENetReceiveResult::None;
        if (ErrorCode == WSAECONNRESET)
        {
            LastError.clear();
            return ENetReceiveResult::None;
        }
        SetSocketError("recvfrom", ErrorCode);
        return ENetReceiveResult::Error;
    }
    if (Received <= 0 || Received > static_cast<int>(MaxNetDatagramBytes))
    {
        LastError = "received an invalid UDP datagram size";
        return ENetReceiveResult::Error;
    }

    std::array<char, INET_ADDRSTRLEN> Host {};
    inet_ntop(AF_INET, &RemoteAddress.sin_addr, Host.data(),
        static_cast<DWORD>(Host.size()));
    OutRemoteAddress.Host = Host.data();
    OutRemoteAddress.Port = ntohs(RemoteAddress.sin_port);
    OutBytes.assign(Buffer.begin(), Buffer.begin() + Received);
    return ENetReceiveResult::Packet;
#else
    return ENetReceiveResult::Error;
#endif
}

const FNetAddress& FUdpTransport::GetLocalAddress() const
{
    return LocalAddress;
}

const std::string& FUdpTransport::GetLastError() const
{
    return LastError;
}

void FUdpTransport::SetSocketError(std::string_view Context, int ErrorCode)
{
    LastError = std::format("{} failed with Winsock error {}", Context, ErrorCode);
}
}
