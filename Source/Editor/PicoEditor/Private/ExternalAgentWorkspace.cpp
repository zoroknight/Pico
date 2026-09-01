#include "ExternalAgentWorkspace.h"

#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"
#include "Pico/Editor/EditorAgentHost.h"
#include "Pico/Editor/EditorAgentExecutionService.h"
#include "Pico/Editor/ExternalAgentConnector.h"
#include "Pico/Mcp/McpServerCore.h"
#include "Pico/McpAdapter/McpAgentToolsetAdapter.h"
#include "Pico/McpHttp/McpHttpServer.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

std::string LowerAscii(std::string Text)
{
    std::transform(Text.begin(), Text.end(), Text.begin(),
        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Text;
}
}

struct FExternalAgentWorkspace::FImpl
{
    explicit FImpl(FEditorAgentHost* InHost)
        : Host(InHost)
    {
        Connectors.push_back(std::make_unique<FCodexExternalAgentConnector>());
        Initialize();
    }

    std::filesystem::path SettingsPath() const
    {
        const auto& Root = FPaths::GetEngineRootDir();
        return Root.empty() ? std::filesystem::path {}
            : Root / "Saved/Editor/McpServer.ini";
    }

    std::string EndpointUrl() const
    {
        return "http://127.0.0.1:" + std::to_string(Port) + Endpoint;
    }

    void SaveSettings()
    {
        const auto Path = SettingsPath();
        if (Path.empty()) return;
        FConfigFile Config;
        Config.SetString("Server", "Enabled", bEnabled ? "true" : "false");
        Config.SetString("Server", "Port", std::to_string(Port));
        Config.SetString("Server", "Endpoint", Endpoint);
        Config.SetString("Server", "BearerToken", BearerToken);
        if (Adapter)
        {
            for (const std::string& Toolset : Adapter->GetToolsetNames())
                Config.SetString("Toolsets", Toolset,
                    Adapter->IsToolsetEnabled(Toolset) ? "true" : "false");
        }
        if (!Config.Save(Path)) UiError = "Could not save MCP host settings";
    }

    bool StartServer()
    {
        if (!Server) return false;
        FMcpHttpServerConfig Config;
        Config.Port = static_cast<std::uint16_t>(Port);
        Config.Endpoint = Endpoint;
        Config.BearerToken = BearerToken;
        std::string Error;
        if (!Server->Start(std::move(Config), &Error))
        {
            UiError = std::move(Error);
            return false;
        }
        UiError.clear();
        return true;
    }

    void Initialize()
    {
        if (!Host)
        {
            UiError = "Editor Agent Host is unavailable";
            return;
        }
        Adapter = std::make_unique<FMcpAgentToolsetAdapter>(
            Host->GetExecutionService(), Host->GetTools().BuildToolCatalogJson(),
            "1.0.0", [this](const FMcpToolCallContext& Context)
            {
                Host->ConfigureSession("mcp:" + Context.PeerId,
                    EAgentTurnIntent::General);
            });
        if (!Adapter->IsValid())
        {
            UiError = Adapter->GetError();
            return;
        }
        Core = std::make_unique<FMcpServerCore>(*Adapter,
            FMcpServerInfo {"PicoEditor", "0.1.0",
                "Pico Editor Agent Host exposed through MCP."});
        Server = std::make_unique<FMcpHttpServer>(*Core);

        FConfigFile Config;
        const auto Path = SettingsPath();
        if (!Path.empty()) Config.Load(Path);
        bEnabled = Config.GetBool("Server", "Enabled", false);
        Port = std::clamp(Config.GetInt("Server", "Port", 8765), 1024, 65535);
        Endpoint = Config.GetString("Server", "Endpoint", "/mcp");
        std::snprintf(EndpointInput.data(), EndpointInput.size(), "%s", Endpoint.c_str());
        BearerToken = Config.GetString("Server", "BearerToken", "");
        for (const auto& [Toolset, Enabled] : Config.GetSectionEntries("Toolsets"))
        {
            const std::string Value = LowerAscii(Enabled);
            Adapter->SetToolsetEnabled(Toolset,
                Value == "true" || Value == "1" || Value == "yes" || Value == "on");
        }
        if (BearerToken.size() < 32)
        {
            BearerToken = GenerateMcpBearerToken();
            SaveSettings();
        }
        if (bEnabled && !StartServer()) bEnabled = false;
    }

    void Draw(bool* Open)
    {
        if (!ImGui::Begin("External Agents", Open))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Editor Agent Host");
        ImGui::TextDisabled(
            "External clients own their model, reasoning, conversation, and UI.");
        ImGui::Separator();

        bool Enabled = bEnabled;
        if (ImGui::Checkbox("Enabled##ExternalMcp", &Enabled))
        {
            if (Enabled) bEnabled = StartServer();
            else
            {
                if (Server) Server->Stop();
                bEnabled = false;
            }
            SaveSettings();
        }
        ImGui::BeginDisabled(bEnabled);
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::InputInt("Port", &Port)) Port = std::clamp(Port, 1024, 65535);
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("Endpoint", EndpointInput.data(), EndpointInput.size()))
            Endpoint = EndpointInput.data();
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
        ImGui::EndDisabled();

        const FMcpHttpServerStatus ServerStatus = Server
            ? Server->GetStatus() : FMcpHttpServerStatus {};
        ImGui::SameLine();
        ImGui::TextColored(ServerStatus.bRunning
                ? ImVec4(0.36f, 0.82f, 0.48f, 1.0f)
                : ImVec4(0.72f, 0.72f, 0.72f, 1.0f),
            ServerStatus.bRunning ? "Running" : "Stopped");
        ImGui::Text("Endpoint: %s", EndpointUrl().c_str());
        ImGui::Text("Sessions: %llu | Accepted: %llu | Rejected: %llu | Active: %llu",
            static_cast<unsigned long long>(ServerStatus.ActiveSessions),
            static_cast<unsigned long long>(ServerStatus.AcceptedRequests),
            static_cast<unsigned long long>(ServerStatus.RejectedRequests),
            static_cast<unsigned long long>(ServerStatus.ActiveRequests));

        if (ImGui::Button("Copy Generic Config"))
        {
            const FJson Config = {{"mcpServers", {{"pico-editor", {
                {"type", "http"}, {"url", EndpointUrl()},
                {"headers", {{"Authorization", "Bearer " + BearerToken}}}}}}}};
            ImGui::SetClipboardText(Config.dump(2).c_str());
            UiStatus = "Generic MCP config copied";
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(bEnabled);
        if (ImGui::Button("Regenerate Token"))
        {
            BearerToken = GenerateMcpBearerToken();
            SaveSettings();
            UiStatus = "MCP token regenerated";
        }
        ImGui::EndDisabled();

        if (Adapter && ImGui::TreeNode("Exposed Toolsets"))
        {
            for (const std::string& Toolset : Adapter->GetToolsetNames())
            {
                bool ToolsetEnabled = Adapter->IsToolsetEnabled(Toolset);
                if (ImGui::Checkbox(Toolset.c_str(), &ToolsetEnabled))
                {
                    Adapter->SetToolsetEnabled(Toolset, ToolsetEnabled);
                    SaveSettings();
                }
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Client Adapters");
        for (const auto& Connector : Connectors)
        {
            ImGui::PushID(Connector->GetId().data());
            ImGui::Text("%s", Connector->GetDisplayName().data());
            ImGui::SameLine();
            if (ImGui::Button("Copy Config"))
            {
                const std::string Config = Connector->GetClientConfig(EndpointUrl());
                ImGui::SetClipboardText(Config.c_str());
                UiStatus = std::string(Connector->GetDisplayName()) + " config copied";
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!ServerStatus.bRunning);
            if (ImGui::Button("Launch CLI")) Launch(*Connector, false);
            ImGui::SameLine();
            if (ImGui::Button("Launch App")) Launch(*Connector, true);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::TextDisabled(
            "Client adapters only export configuration or launch a process; they never own editor tools.");

        if (!UiStatus.empty())
            ImGui::TextColored(ImVec4(0.50f, 0.82f, 0.62f, 1.0f), "%s", UiStatus.c_str());
        const std::string Error = !UiError.empty() ? UiError : ServerStatus.LastError;
        if (!Error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f), "%s", Error.c_str());
        ImGui::TextDisabled("Settings: %s", SettingsPath().string().c_str());
        ImGui::End();
    }

    void Launch(const IExternalAgentConnector& Connector, bool Desktop)
    {
        FExternalAgentLaunchContext Context {
            FPaths::GetProjectRootDir(), EndpointUrl(), BearerToken};
        std::string Error;
        FProcessHandle Process = Desktop
            ? Connector.LaunchDesktop(Context, &Error)
            : Connector.LaunchCli(Context, &Error);
        if (Process.IsValid())
        {
            UiError.clear();
            UiStatus = std::string(Connector.GetDisplayName())
                + (Desktop ? " app launched" : " CLI launched");
        }
        else UiError = std::move(Error);
    }

    void Shutdown()
    {
        if (bShutdown) return;
        bShutdown = true;
        if (Server) Server->Stop();
    }

    FEditorAgentHost* Host = nullptr;
    std::unique_ptr<FMcpAgentToolsetAdapter> Adapter;
    std::unique_ptr<FMcpServerCore> Core;
    std::unique_ptr<FMcpHttpServer> Server;
    std::vector<std::unique_ptr<IExternalAgentConnector>> Connectors;
    std::array<char, 128> EndpointInput {};
    bool bEnabled = false;
    bool bShutdown = false;
    int Port = 8765;
    std::string Endpoint = "/mcp";
    std::string BearerToken;
    std::string UiStatus;
    std::string UiError;
};

FExternalAgentWorkspace::FExternalAgentWorkspace(FEditorAgentHost* Host)
    : Impl(std::make_unique<FImpl>(Host))
{
}
FExternalAgentWorkspace::~FExternalAgentWorkspace() { Shutdown(); }
void FExternalAgentWorkspace::Draw(bool* Open) { if (Impl) Impl->Draw(Open); }
void FExternalAgentWorkspace::Shutdown() { if (Impl) Impl->Shutdown(); }
}
