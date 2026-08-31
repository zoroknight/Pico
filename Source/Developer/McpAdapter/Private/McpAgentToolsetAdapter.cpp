#include "Pico/McpAdapter/McpAgentToolsetAdapter.h"

#include "Pico/Agent/AgentTypes.h"
#include "Pico/Tasks/TaskSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <atomic>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Pico
{
using FJson = nlohmann::json;

namespace
{
struct FToolEntry
{
    std::string Name;
    std::string Description;
    std::string Permission;
    FJson InputSchema = FJson::object();
};

struct FToolsetEntry
{
    std::string Name;
    std::string Version;
    bool bEnabled = true;
    std::vector<FToolEntry> Tools;
};

FMcpToolCallResult Failure(std::string Message, FJson Details = FJson::object())
{
    Details["succeeded"] = false;
    Details["error"] = Message;
    return {true, std::move(Message), Details.dump()};
}

std::string StableCallId(std::string_view InvocationIdentity,
    std::string_view Toolset, std::string_view Tool)
{
    std::uint64_t Hash = 1469598103934665603ULL;
    const auto Add = [&Hash](std::string_view Value)
    {
        for (const unsigned char Character : Value)
        {
            Hash ^= Character;
            Hash *= 1099511628211ULL;
        }
        Hash ^= 0xff;
        Hash *= 1099511628211ULL;
    };
    Add(InvocationIdentity);
    Add(Toolset);
    Add(Tool);
    std::ostringstream Stream;
    Stream << "mcp-" << std::hex << std::setw(16) << std::setfill('0') << Hash;
    return Stream.str();
}

bool HasOnlyFields(const FJson& Object,
    std::initializer_list<std::string_view> Fields)
{
    std::unordered_set<std::string> Allowed;
    for (std::string_view Field : Fields) Allowed.emplace(Field);
    for (auto It = Object.begin(); It != Object.end(); ++It)
    {
        if (!Allowed.contains(It.key())) return false;
    }
    return true;
}

FJson ParseOrString(std::string_view Text, FJson Fallback = FJson::object())
{
    if (Text.empty()) return Fallback;
    FJson Parsed = FJson::parse(Text, nullptr, false);
    return Parsed.is_discarded() ? FJson(std::string(Text)) : Parsed;
}
}

class FMcpAgentToolsetAdapter::FImpl
{
public:
    FImpl(IAgentToolExecutor& InExecutor,
        std::string ToolCatalogJson, std::string ToolsetVersion,
        FMcpAgentToolsetAdapter::FExecutionContextBinder InExecutionContextBinder)
        : Executor(InExecutor)
        , ExecutionContextBinder(std::move(InExecutionContextBinder))
    {
        FJson Catalog = FJson::parse(ToolCatalogJson, nullptr, false);
        if (Catalog.is_discarded() || !Catalog.is_array())
        {
            Error = "Agent tool catalog must be a JSON array";
            return;
        }
        for (const FJson& Item : Catalog)
        {
            if (!Item.is_object()
                || !Item.contains("name") || !Item["name"].is_string()
                || !Item.contains("capability_provider")
                || !Item["capability_provider"].is_string()
                || !Item.contains("input_schema")
                || !Item["input_schema"].is_object())
            {
                Error = "Agent tool catalog contains an invalid entry";
                Toolsets.clear();
                return;
            }
            const std::string Provider =
                Item["capability_provider"].get<std::string>();
            const std::string ToolName = Item["name"].get<std::string>();
            if (Provider.empty() || ToolName.empty())
            {
                Error = "Agent tool catalog contains an empty provider or tool name";
                Toolsets.clear();
                return;
            }
            FToolsetEntry& Toolset = Toolsets[Provider];
            Toolset.Name = Provider;
            Toolset.Version = ToolsetVersion;
            if (std::any_of(Toolset.Tools.begin(), Toolset.Tools.end(),
                [&ToolName](const FToolEntry& Tool) {
                    return Tool.Name == ToolName;
                }))
            {
                Error = "Agent tool catalog contains duplicate tool names";
                Toolsets.clear();
                return;
            }
            Toolset.Tools.push_back({ToolName,
                Item.value("description", ""), Item.value("permission", ""),
                Item["input_schema"]});
        }
        for (auto& [Name, Toolset] : Toolsets)
        {
            (void)Name;
            std::sort(Toolset.Tools.begin(), Toolset.Tools.end(),
                [](const FToolEntry& Left, const FToolEntry& Right) {
                    return Left.Name < Right.Name;
                });
        }
        if (Toolsets.empty()) Error = "Agent tool catalog contains no toolsets";
    }

    std::vector<FMcpToolDescriptor> ListTools() const
    {
        return {
            {"list_toolsets", "List Pico Toolsets",
                "List the versioned Pico capability toolsets available through this server.",
                R"({"type":"object","additionalProperties":false})"},
            {"describe_toolset", "Describe Pico Toolset",
                "Describe the tools and input schemas in one Pico capability toolset.",
                R"({"type":"object","properties":{"toolset":{"type":"string","minLength":1,"maxLength":128}},"required":["toolset"],"additionalProperties":false})"},
            {"call_tool", "Call Pico Tool",
                "Call one tool from an enabled Pico capability toolset through the protected Agent execution pipeline.",
                R"({"type":"object","properties":{"toolset":{"type":"string","minLength":1,"maxLength":128},"name":{"type":"string","minLength":1,"maxLength":128},"arguments":{"type":"object"},"operation_id":{"type":"string","minLength":1,"maxLength":128}},"required":["toolset","name","arguments"],"additionalProperties":false})"}};
    }

    FMcpToolCallResult CallTool(
        const FMcpToolCallContext& Context,
        std::string_view Name,
        std::string_view ArgumentsJson,
        const FMcpCancellationToken& Cancellation)
    {
        FJson Arguments = FJson::parse(ArgumentsJson, nullptr, false);
        if (Arguments.is_discarded() || !Arguments.is_object())
            return Failure("MCP meta-tool arguments must be an object");
        if (Name == "list_toolsets") return ListToolsets(Arguments);
        if (Name == "describe_toolset") return DescribeToolset(Arguments);
        if (Name == "call_tool")
            return ExecuteTool(Context, Arguments, Cancellation);
        return Failure("Unknown Pico MCP meta-tool: " + std::string(Name));
    }

    FMcpToolCallResult ListToolsets(const FJson& Arguments) const
    {
        if (!Arguments.empty())
            return Failure("list_toolsets does not accept arguments");
        FJson Items = FJson::array();
        std::lock_guard Lock(Mutex);
        for (const std::string& Name : SortedToolsetNames())
        {
            const FToolsetEntry& Toolset = Toolsets.at(Name);
            Items.push_back({{"name", Toolset.Name},
                {"version", Toolset.Version}, {"enabled", Toolset.bEnabled},
                {"tool_count", Toolset.Tools.size()}});
        }
        FJson Result {{"toolsets", std::move(Items)}};
        return {false, "Listed Pico capability toolsets", Result.dump()};
    }

    FMcpToolCallResult DescribeToolset(const FJson& Arguments) const
    {
        if (!HasOnlyFields(Arguments, {"toolset"})
            || !Arguments.contains("toolset")
            || !Arguments["toolset"].is_string())
        {
            return Failure("describe_toolset requires only a string toolset field");
        }
        std::lock_guard Lock(Mutex);
        const auto It = Toolsets.find(Arguments["toolset"].get<std::string>());
        if (It == Toolsets.end()) return Failure("Unknown Pico toolset");
        FJson Tools = FJson::array();
        for (const FToolEntry& Tool : It->second.Tools)
        {
            Tools.push_back({{"name", Tool.Name},
                {"description", Tool.Description},
                {"permission", Tool.Permission},
                {"input_schema", Tool.InputSchema}});
        }
        FJson Result {{"name", It->second.Name},
            {"version", It->second.Version}, {"enabled", It->second.bEnabled},
            {"tools", std::move(Tools)}};
        return {false, "Described Pico capability toolset", Result.dump()};
    }

    FMcpToolCallResult ExecuteTool(
        const FMcpToolCallContext& Context,
        const FJson& Arguments,
        const FMcpCancellationToken& Cancellation)
    {
        if (!HasOnlyFields(Arguments,
                {"toolset", "name", "arguments", "operation_id"})
            || !Arguments.contains("toolset")
            || !Arguments["toolset"].is_string()
            || !Arguments.contains("name") || !Arguments["name"].is_string()
            || !Arguments.contains("arguments")
            || !Arguments["arguments"].is_object())
        {
            return Failure("call_tool requires toolset, name, and object arguments");
        }
        if (Arguments.contains("operation_id")
            && (!Arguments["operation_id"].is_string()
                || Arguments["operation_id"].get_ref<const std::string&>().empty()
                || Arguments["operation_id"].get_ref<const std::string&>().size() > 128))
        {
            return Failure("operation_id must be a non-empty string of at most 128 bytes");
        }
        const std::string ToolsetName = Arguments["toolset"].get<std::string>();
        const std::string ToolName = Arguments["name"].get<std::string>();
        {
            std::lock_guard Lock(Mutex);
            const auto It = Toolsets.find(ToolsetName);
            if (It == Toolsets.end()) return Failure("Unknown Pico toolset");
            if (!It->second.bEnabled) return Failure("Pico toolset is disabled");
            if (std::none_of(It->second.Tools.begin(), It->second.Tools.end(),
                [&ToolName](const FToolEntry& Tool) {
                    return Tool.Name == ToolName;
                }))
            {
                return Failure("Tool does not belong to the selected Pico toolset");
            }
        }
        if (Cancellation.IsCancellationRequested() || Cancellation.IsExpired())
            return Failure("Pico tool call was cancelled before execution");

        std::string InvocationIdentity;
        if (Arguments.contains("operation_id"))
        {
            InvocationIdentity = "explicit:"
                + Arguments["operation_id"].get<std::string>();
        }
        else
        {
            InvocationIdentity = "ephemeral:" + Context.PeerId + ":"
                + Context.RequestId + ":"
                + std::to_string(NextInvocation.fetch_add(1));
        }
        const std::string CallId = StableCallId(
            InvocationIdentity, ToolsetName, ToolName);
        const FAgentToolCall Call {
            CallId, ToolName, Arguments["arguments"].dump()};
        const std::string RunId = "mcp-run-" + CallId.substr(4);
        FCancellationToken AgentCancellation([&Cancellation]() {
            return Cancellation.IsCancellationRequested()
                || Cancellation.IsExpired();
        });

        if (ExecutionContextBinder) ExecutionContextBinder(Context);
        Executor.BeginRun(RunId);
        FAgentToolResult AgentResult;
        try
        {
            if (Executor.RequiresApproval(Call)) Executor.PrepareApproval(Call);
            AgentResult = Executor.Execute(Call, &AgentCancellation);
            NormalizeAgentToolResult(AgentResult);
            if (AgentResult.bSucceeded && !Executor.IsReadOnly(Call))
                Executor.CommitDurableResult(Call);
            Executor.EndRun(RunId, AgentResult.bSucceeded
                ? EAgentStatus::Completed : EAgentStatus::Failed);
        }
        catch (const std::exception& Exception)
        {
            Executor.EndRun(RunId, EAgentStatus::Failed);
            return Failure("Agent execution pipeline threw: "
                + std::string(Exception.what()));
        }
        catch (...)
        {
            Executor.EndRun(RunId, EAgentStatus::Failed);
            return Failure("Agent execution pipeline threw an unknown exception");
        }

        FJson Structured {
            {"toolset", ToolsetName}, {"tool", ToolName},
            {"agent_result", ParseOrString(
                BuildAgentToolResultModelJson(AgentResult))},
            {"trace", ParseOrString(
                Executor.GetLastExecutionTraceJson(), FJson::array())}};
        std::string Text = AgentResult.bSucceeded
            ? "Pico tool completed: " + ToolName
            : (AgentResult.Error.empty()
                ? "Pico tool failed: " + ToolName : AgentResult.Error);
        return {!AgentResult.bSucceeded, std::move(Text), Structured.dump()};
    }

    std::vector<std::string> SortedToolsetNames() const
    {
        std::vector<std::string> Names;
        Names.reserve(Toolsets.size());
        for (const auto& [Name, Toolset] : Toolsets)
        {
            (void)Toolset;
            Names.push_back(Name);
        }
        std::sort(Names.begin(), Names.end());
        return Names;
    }

    IAgentToolExecutor& Executor;
    FMcpAgentToolsetAdapter::FExecutionContextBinder ExecutionContextBinder;
    mutable std::mutex Mutex;
    std::unordered_map<std::string, FToolsetEntry> Toolsets;
    std::string Error;
    std::atomic<std::uint64_t> NextInvocation {1};
};

FMcpAgentToolsetAdapter::FMcpAgentToolsetAdapter(
    IAgentToolExecutor& InExecutor,
    std::string ToolCatalogJson,
    std::string ToolsetVersion,
    FExecutionContextBinder ExecutionContextBinder)
    : Impl(std::make_unique<FImpl>(InExecutor,
        std::move(ToolCatalogJson), std::move(ToolsetVersion),
        std::move(ExecutionContextBinder)))
{
}

FMcpAgentToolsetAdapter::~FMcpAgentToolsetAdapter() = default;

bool FMcpAgentToolsetAdapter::IsValid() const
{
    return Impl && Impl->Error.empty();
}

std::string FMcpAgentToolsetAdapter::GetError() const
{
    return Impl ? Impl->Error : "Pico MCP Agent Adapter is unavailable";
}

std::vector<std::string> FMcpAgentToolsetAdapter::GetToolsetNames() const
{
    if (!Impl) return {};
    std::lock_guard Lock(Impl->Mutex);
    return Impl->SortedToolsetNames();
}

bool FMcpAgentToolsetAdapter::SetToolsetEnabled(
    std::string_view Name, bool bEnabled)
{
    if (!Impl) return false;
    std::lock_guard Lock(Impl->Mutex);
    const auto It = Impl->Toolsets.find(std::string(Name));
    if (It == Impl->Toolsets.end()) return false;
    It->second.bEnabled = bEnabled;
    return true;
}

bool FMcpAgentToolsetAdapter::IsToolsetEnabled(std::string_view Name) const
{
    if (!Impl) return false;
    std::lock_guard Lock(Impl->Mutex);
    const auto It = Impl->Toolsets.find(std::string(Name));
    return It != Impl->Toolsets.end() && It->second.bEnabled;
}

std::vector<FMcpToolDescriptor> FMcpAgentToolsetAdapter::ListTools() const
{
    return Impl ? Impl->ListTools() : std::vector<FMcpToolDescriptor> {};
}

FMcpToolCallResult FMcpAgentToolsetAdapter::CallTool(
    const FMcpToolCallContext& Context,
    std::string_view Name,
    std::string_view ArgumentsJson,
    const FMcpCancellationToken& Cancellation)
{
    if (!Impl || !Impl->Error.empty())
        return Failure(GetError());
    return Impl->CallTool(Context, Name, ArgumentsJson, Cancellation);
}
}
