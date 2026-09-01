#include "Pico/Editor/ExternalAgentConnector.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <vector>

namespace Pico
{
namespace
{
std::optional<std::string> EnvironmentString(const char* Name)
{
#ifdef _WIN32
    char* Value = nullptr;
    std::size_t Length = 0;
    if (_dupenv_s(&Value, &Length, Name) != 0 || Value == nullptr)
        return std::nullopt;
    std::string Result(Value);
    std::free(Value);
    return Result;
#else
    const char* Value = std::getenv(Name);
    if (Value == nullptr || *Value == '\0') return std::nullopt;
    return std::string(Value);
#endif
}

std::optional<std::filesystem::path> EnvironmentPath(const char* Name)
{
    const auto Value = EnvironmentString(Name);
    return Value ? std::optional<std::filesystem::path>(*Value) : std::nullopt;
}

bool IsExecutableFile(const std::filesystem::path& Path)
{
    std::error_code Error;
    return std::filesystem::is_regular_file(Path, Error) && !Error;
}

bool ContainsConfig(const std::filesystem::path& Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) return false;
    const std::string Text {
        std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
    return Text.find("[mcp_servers.pico_editor]") != std::string::npos
        && Text.find("bearer_token_env_var = \"PICO_MCP_BEARER_TOKEN\"")
            != std::string::npos;
}

std::string TomlString(std::string_view Value)
{
    std::string Result = "\"";
    for (const char Character : Value)
    {
        if (Character == '\\' || Character == '"') Result.push_back('\\');
        Result.push_back(Character);
    }
    Result.push_back('"');
    return Result;
}

std::vector<std::string> CodexMcpOverrides(std::string_view EndpointUrl)
{
    return {
        "-c", "mcp_servers.pico_editor.url=" + TomlString(EndpointUrl),
        "-c", "mcp_servers.pico_editor.bearer_token_env_var="
            + TomlString(FCodexExternalAgentConnector::TokenEnvironmentVariable),
        "-c", "mcp_servers.pico_editor.enabled=true",
        "-c", "mcp_servers.pico_editor.enabled_tools=[\"list_toolsets\","
            "\"describe_toolset\",\"call_tool\"]",
        "-c", "mcp_servers.pico_editor.default_tools_approval_mode=\"approve\"",
        "-c", "mcp_servers.pico_editor.startup_timeout_sec=10",
        "-c", "mcp_servers.pico_editor.tool_timeout_sec=300"};
}
}

std::string_view FCodexExternalAgentConnector::GetId() const { return "codex"; }
std::string_view FCodexExternalAgentConnector::GetDisplayName() const
{
    return "Codex";
}

std::string FCodexExternalAgentConnector::GetClientConfig(
    std::string_view EndpointUrl) const
{
    return "[mcp_servers.pico_editor]\n"
        "url = \"" + std::string(EndpointUrl) + "\"\n"
        "bearer_token_env_var = \"PICO_MCP_BEARER_TOKEN\"\n"
        "enabled = true\n"
        "required = false\n"
        "enabled_tools = [\"list_toolsets\", \"describe_toolset\", \"call_tool\"]\n"
        "default_tools_approval_mode = \"approve\"\n"
        "startup_timeout_sec = 10\n"
        "tool_timeout_sec = 300\n";
}

bool FCodexExternalAgentConnector::HasClientConfig(
    const std::filesystem::path& ProjectRoot,
    std::filesystem::path* OutPath) const
{
    std::vector<std::filesystem::path> Candidates;
    if (!ProjectRoot.empty())
    {
        std::filesystem::path Directory =
            std::filesystem::absolute(ProjectRoot).lexically_normal();
        while (!Directory.empty())
        {
            Candidates.push_back(Directory / ".codex" / "config.toml");
            const std::filesystem::path Parent = Directory.parent_path();
            if (Parent == Directory) break;
            Directory = Parent;
        }
    }
    if (const auto UserProfile = EnvironmentPath("USERPROFILE"))
        Candidates.push_back(*UserProfile / ".codex" / "config.toml");
    if (const auto Home = EnvironmentPath("HOME"))
        Candidates.push_back(*Home / ".codex" / "config.toml");
    for (const std::filesystem::path& Candidate : Candidates)
    {
        if (!ContainsConfig(Candidate)) continue;
        if (OutPath) *OutPath = Candidate;
        return true;
    }
    if (OutPath) OutPath->clear();
    return false;
}

std::filesystem::path FCodexExternalAgentConnector::FindExecutable()
{
    if (const auto Override = EnvironmentPath("PICO_CODEX_EXECUTABLE");
        Override && IsExecutableFile(*Override))
    {
        return std::filesystem::absolute(*Override).lexically_normal();
    }
    if (const auto PathValue = EnvironmentString("PATH"))
    {
        std::stringstream Paths(*PathValue);
        std::string DirectoryText;
        while (std::getline(Paths, DirectoryText, ';'))
        {
            const std::filesystem::path Candidate =
                std::filesystem::path(DirectoryText) / "codex.exe";
            if (IsExecutableFile(Candidate))
                return std::filesystem::absolute(Candidate).lexically_normal();
        }
    }
    if (const auto AppData = EnvironmentPath("APPDATA"))
    {
        const std::filesystem::path Root = *AppData / "npm" / "node_modules"
            / "@openai" / "codex" / "node_modules" / "@openai";
        for (const char* Package : {"codex-win32-x64", "codex-win32-arm64"})
        {
            std::error_code Error;
            const auto Vendor = Root / Package / "vendor";
            if (!std::filesystem::is_directory(Vendor, Error)) continue;
            for (const auto& Entry : std::filesystem::recursive_directory_iterator(
                    Vendor, std::filesystem::directory_options::skip_permission_denied,
                    Error))
            {
                if (Error) break;
                if (Entry.path().filename() == "codex.exe"
                    && IsExecutableFile(Entry.path()))
                    return std::filesystem::absolute(Entry.path()).lexically_normal();
            }
        }
    }
    return {};
}

FProcessHandle FCodexExternalAgentConnector::LaunchCli(
    const FExternalAgentLaunchContext& Context, std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (Context.BearerToken.size() < 32)
    {
        if (OutError) *OutError = "Pico MCP bearer token is invalid";
        return {};
    }
    const auto Executable = FindExecutable();
    if (Executable.empty())
    {
        if (OutError) *OutError = "Codex CLI was not found";
        return {};
    }
    FProcessLaunchOptions Options;
    Options.bCreateNewConsole = true;
    Options.EnvironmentOverrides.emplace_back(
        TokenEnvironmentVariable, Context.BearerToken);
    return FPlatformProcess::CreateProcess(Executable,
        CodexMcpOverrides(Context.EndpointUrl), Context.ProjectRoot,
        {}, OutError, nullptr, Options);
}

FProcessHandle FCodexExternalAgentConnector::LaunchDesktop(
    const FExternalAgentLaunchContext& Context, std::string* OutError) const
{
    if (OutError) OutError->clear();
    if (Context.BearerToken.size() < 32)
    {
        if (OutError) *OutError = "Pico MCP bearer token is invalid";
        return {};
    }
    const auto Executable = FindExecutable();
    if (Executable.empty())
    {
        if (OutError) *OutError = "Codex CLI was not found";
        return {};
    }
    FProcessLaunchOptions Options;
    Options.EnvironmentOverrides.emplace_back(
        TokenEnvironmentVariable, Context.BearerToken);
    std::vector<std::string> Arguments = CodexMcpOverrides(Context.EndpointUrl);
    Arguments.emplace_back("app");
    Arguments.emplace_back(Context.ProjectRoot.string());
    return FPlatformProcess::CreateProcess(Executable, Arguments, Context.ProjectRoot,
        {}, OutError, nullptr, Options);
}
}
