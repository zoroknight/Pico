#include "Pico/Editor/PlaySession.h"

#include "Pico/Core/Config.h"

#include <algorithm>
#include <fstream>

namespace Pico
{
namespace
{
std::string MakeArgument(std::string_view Name, int Value)
{
    return "-" + std::string(Name) + "=" + std::to_string(Value);
}

void AddCommonArguments(
    FPlayProcessSpec& Spec,
    const FPlaySessionLaunchRequest& Request,
    int WindowIndex)
{
    Spec.Arguments.push_back(Request.ProjectFile.string());
    Spec.Arguments.push_back("-map=" + Request.MapAssetPath);
    Spec.Arguments.push_back("-instance=" + Spec.Label);
    Spec.Arguments.push_back(MakeArgument(
        "windowwidth", Request.Settings.ClientWindowWidth));
    Spec.Arguments.push_back(MakeArgument(
        "windowheight", Request.Settings.ClientWindowHeight));
    Spec.Arguments.push_back(MakeArgument("windowx", 40 + WindowIndex * 36));
    Spec.Arguments.push_back(MakeArgument("windowy", 40 + WindowIndex * 36));
    // Each packet crosses two simulated one-way legs in a client/server RTT.
    Spec.Arguments.push_back(MakeArgument(
        "netlatency", (Request.Settings.NetworkLatencyMs + 1) / 2));
    Spec.Arguments.push_back(MakeArgument(
        "netjitter", Request.Settings.NetworkJitterMs));
    Spec.Arguments.push_back(MakeArgument(
        "netloss", Request.Settings.PacketLossPercent));
}
}

const char* ToString(EEditorPlayNetMode Mode)
{
    switch (Mode)
    {
    case EEditorPlayNetMode::Standalone: return "Standalone";
    case EEditorPlayNetMode::SeparateServer: return "SeparateServer";
    case EEditorPlayNetMode::ListenServer: return "ListenServer";
    }
    return "Standalone";
}

void FPlaySessionSettings::Clamp()
{
    PlayerCount = std::clamp(PlayerCount, 1, 4);
    ServerPort = std::clamp(ServerPort, 1, 65535);
    ClientWindowWidth = std::clamp(ClientWindowWidth, 320, 3840);
    ClientWindowHeight = std::clamp(ClientWindowHeight, 240, 2160);
    NetworkLatencyMs = std::clamp(NetworkLatencyMs, 0, 2000);
    NetworkJitterMs = std::clamp(NetworkJitterMs, 0, 1000);
    PacketLossPercent = std::clamp(PacketLossPercent, 0, 100);
    if (NetMode == EEditorPlayNetMode::Standalone) PlayerCount = 1;
}

bool FPlaySessionSettings::Validate(std::string& OutError) const
{
    OutError.clear();
    if (NetMode == EEditorPlayNetMode::ListenServer)
    {
        OutError = "Listen Server is reserved for the replication milestone";
        return false;
    }
    if (PlayerCount < 1 || PlayerCount > 4)
    {
        OutError = "player count must be between 1 and 4";
        return false;
    }
    if (NetMode == EEditorPlayNetMode::Standalone && PlayerCount != 1)
    {
        OutError = "Standalone Play supports exactly one player";
        return false;
    }
    if (ServerPort < 1 || ServerPort > 65535)
    {
        OutError = "server port must be between 1 and 65535";
        return false;
    }
    if (ClientWindowWidth < 320 || ClientWindowWidth > 3840
        || ClientWindowHeight < 240 || ClientWindowHeight > 2160)
    {
        OutError = "client window size is outside the supported range";
        return false;
    }
    if (NetworkLatencyMs < 0 || NetworkLatencyMs > 2000
        || NetworkJitterMs < 0 || NetworkJitterMs > 1000
        || PacketLossPercent < 0 || PacketLossPercent > 100)
    {
        OutError = "network simulation settings are outside the supported range";
        return false;
    }
    return true;
}

bool FPlaySessionSettings::Load(const std::filesystem::path& FilePath)
{
    FConfigFile Config;
    if (!Config.Load(FilePath)) return false;
    const std::string Mode = Config.GetString("Play", "NetMode", "Standalone");
    if (Mode == "SeparateServer" || Mode == "DedicatedServer")
        NetMode = EEditorPlayNetMode::SeparateServer;
    else if (Mode == "ListenServer") NetMode = EEditorPlayNetMode::ListenServer;
    else NetMode = EEditorPlayNetMode::Standalone;
    PlayerCount = Config.GetInt("Play", "PlayerCount", 1);
    ServerPort = Config.GetInt("Play", "ServerPort", 17777);
    ClientWindowWidth = Config.GetInt("Play", "ClientWindowWidth", 960);
    ClientWindowHeight = Config.GetInt("Play", "ClientWindowHeight", 540);
    NetworkLatencyMs = Config.GetInt("Play", "NetworkLatencyMs", 0);
    NetworkJitterMs = Config.GetInt("Play", "NetworkJitterMs", 0);
    PacketLossPercent = Config.GetInt("Play", "PacketLossPercent", 0);
    Clamp();
    return true;
}

bool FPlaySessionSettings::Save(const std::filesystem::path& FilePath) const
{
    FConfigFile Config;
    Config.SetString("Play", "NetMode", ToString(NetMode));
    Config.SetString("Play", "PlayerCount", std::to_string(PlayerCount));
    Config.SetString("Play", "ServerPort", std::to_string(ServerPort));
    Config.SetString(
        "Play", "ClientWindowWidth", std::to_string(ClientWindowWidth));
    Config.SetString(
        "Play", "ClientWindowHeight", std::to_string(ClientWindowHeight));
    Config.SetString(
        "Play", "NetworkLatencyMs", std::to_string(NetworkLatencyMs));
    Config.SetString(
        "Play", "NetworkJitterMs", std::to_string(NetworkJitterMs));
    Config.SetString(
        "Play", "PacketLossPercent", std::to_string(PacketLossPercent));
    return Config.Save(FilePath);
}

bool BuildPlayProcessSpecs(
    const FPlaySessionLaunchRequest& Request,
    std::vector<FPlayProcessSpec>& OutSpecs,
    std::string& OutError)
{
    OutSpecs.clear();
    if (!Request.Settings.Validate(OutError)) return false;
    if (!std::filesystem::is_regular_file(Request.Executable))
    {
        OutError = "project game executable does not exist";
        return false;
    }
    if (!std::filesystem::is_regular_file(Request.ProjectFile))
    {
        OutError = "Pico project descriptor does not exist";
        return false;
    }
    if (Request.MapAssetPath.empty())
    {
        OutError = "Play requires a saved World asset path";
        return false;
    }

    if (Request.Settings.NetMode == EEditorPlayNetMode::Standalone)
    {
        FPlayProcessSpec Spec;
        Spec.Role = EPlayProcessRole::Standalone;
        Spec.Label = "Standalone";
        Spec.LogFile = Request.LogDirectory / "Standalone.log";
        AddCommonArguments(Spec, Request, 0);
        OutSpecs.push_back(std::move(Spec));
        return true;
    }

    FPlayProcessSpec Server;
    Server.Role = EPlayProcessRole::Server;
    Server.Label = "Server";
    Server.LogFile = Request.LogDirectory / "Server.log";
    AddCommonArguments(Server, Request, 0);
    Server.Arguments.push_back("-server");
    Server.Arguments.push_back(MakeArgument("port", Request.Settings.ServerPort));
    OutSpecs.push_back(std::move(Server));

    for (int Index = 0; Index < Request.Settings.PlayerCount; ++Index)
    {
        FPlayProcessSpec Client;
        Client.Role = EPlayProcessRole::Client;
        Client.Label = "Client_" + std::to_string(Index + 1);
        Client.LogFile = Request.LogDirectory / (Client.Label + ".log");
        AddCommonArguments(Client, Request, Index + 1);
        Client.Arguments.push_back("-client=127.0.0.1");
        Client.Arguments.push_back(MakeArgument("port", Request.Settings.ServerPort));
        OutSpecs.push_back(std::move(Client));
    }
    return true;
}

FPlaySession::~FPlaySession()
{
    Stop();
}

bool FPlaySession::Start(
    const FPlaySessionLaunchRequest& Request,
    std::string& OutError)
{
    if (IsActive())
    {
        OutError = "a Play Session is already active";
        return false;
    }
    std::vector<FPlayProcessSpec> Specs;
    if (!BuildPlayProcessSpecs(Request, Specs, OutError)) return false;

    std::error_code DirectoryError;
    std::filesystem::create_directories(Request.LogDirectory, DirectoryError);
    if (DirectoryError)
    {
        OutError = "could not create the Play Session log directory";
        return false;
    }

    LogDirectory = Request.LogDirectory;
    for (const FPlayProcessSpec& Spec : Specs)
    {
        FManagedProcess Managed;
        Managed.Label = Spec.Label;
        Managed.LogFile = Spec.LogFile;
        Managed.Handle = FPlatformProcess::CreateProcess(
            Request.Executable,
            Spec.Arguments,
            Request.WorkingDirectory,
            Spec.LogFile,
            &OutError);
        if (!Managed.Handle.IsValid())
        {
            OutError = "could not start " + Spec.Label + ": " + OutError;
            Stop();
            return false;
        }
        Processes.push_back(std::move(Managed));
    }
    return true;
}

bool FPlaySession::Stop()
{
    bool bStopped = true;
    for (FManagedProcess& Process : Processes)
    {
        if (FPlatformProcess::IsRunning(Process.Handle))
        {
            const bool bTerminated = FPlatformProcess::Terminate(Process.Handle);
            bStopped = bTerminated && bStopped;
            if (bTerminated) FPlatformProcess::WaitForExit(Process.Handle, 2000);
        }
        Process.Handle.Reset();
    }
    Processes.clear();
    return bStopped;
}

std::vector<FPlayProcessExit> FPlaySession::Poll()
{
    std::vector<FPlayProcessExit> Exits;
    for (FManagedProcess& Process : Processes)
    {
        if (!Process.Handle.IsValid() || FPlatformProcess::IsRunning(Process.Handle))
        {
            continue;
        }
        FPlayProcessExit Exit;
        Exit.Label = Process.Label;
        Exit.LogFile = Process.LogFile;
        FPlatformProcess::WaitForExit(Process.Handle, 0, &Exit.ExitCode);
        Process.Handle.Reset();
        Exits.push_back(std::move(Exit));
    }
    std::erase_if(Processes,
        [](const FManagedProcess& Process) { return !Process.Handle.IsValid(); });
    return Exits;
}
}
