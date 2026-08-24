#include "Pico/Agent/AgentRuntime.h"
#include "Pico/Agent/FakeAgentProvider.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
using FJson = nlohmann::json;

class FHostFakeToolExecutor final : public Pico::IAgentToolExecutor
{
public:
    Pico::FAgentToolResult Execute(
        const Pico::FAgentToolCall& Call,
        const Pico::FCancellationToken*) override
    {
        return {Call.Id, true, FJson {{"tool", Call.Name}, {"accepted", true}}.dump(), {}, false};
    }
};

std::string FindArgument(int Argc, char** Argv, std::string_view Prefix)
{
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string Argument = Argv[Index];
        if (Argument.starts_with(Prefix)) return Argument.substr(Prefix.size());
    }
    return {};
}

std::vector<Pico::FFakeAgentStep> BuildScenario(std::string_view Scenario)
{
    if (Scenario == "tool")
    {
        Pico::FAgentProviderResponse Tool;
        Tool.Content = "Calling deterministic fake tool";
        Tool.ToolCalls.push_back({"host-call-1", "fake.echo", R"({"value":"Pico"})"});
        Pico::FAgentProviderResponse Final;
        Final.bFinal = true;
        Final.Content = "Fake tool flow completed";
        return {{Tool, {}}, {Final, {}}};
    }
    if (Scenario == "repair")
    {
        Pico::FAgentProviderResponse Failure;
        Failure.bSucceeded = false;
        Failure.Error = "Injected provider failure";
        Pico::FAgentProviderResponse Final;
        Final.bFinal = true;
        Final.Content = "Recovered after one bounded repair";
        return {{Failure, {}}, {Final, {}}};
    }
    Pico::FAgentProviderResponse Final;
    Final.bFinal = true;
    Final.Content = "Fake provider completed";
    return {{Final, {}}};
}
}

int main(int Argc, char** Argv)
{
    const std::filesystem::path RequestPath = FindArgument(Argc, Argv, "--request=");
    const std::filesystem::path ResponsePath = FindArgument(Argc, Argv, "--response=");
    if (RequestPath.empty() || ResponsePath.empty())
    {
        std::cerr << "Usage: PicoAgentHost --request=<request.json> --response=<response.json>\n";
        return 1;
    }

    try
    {
        std::ifstream RequestStream(RequestPath, std::ios::binary);
        const FJson Request = FJson::parse(RequestStream);
        const std::string SessionId = Request.at("session_id").get<std::string>();
        const std::filesystem::path EventLog = Request.at("event_log").get<std::string>();
        const std::string Prompt = Request.value("prompt", "");
        const std::string Scenario = Request.value("fake_scenario", "final");

        std::string Error;
        auto Session = Pico::FAgentSession::OpenOrCreate(SessionId, EventLog, &Error);
        if (!Session) throw std::runtime_error(Error);
        Pico::FFakeAgentProvider Provider(BuildScenario(Scenario));
        FHostFakeToolExecutor Executor;
        Pico::FAgentRuntime Runtime(*Session, Provider, Executor);
        const Pico::FAgentRunResult Result = Runtime.Run(Prompt);

        std::filesystem::create_directories(ResponsePath.parent_path());
        std::ofstream ResponseStream(ResponsePath, std::ios::binary | std::ios::trunc);
        ResponseStream << FJson {
            {"status", Pico::ToString(Result.Status)}, {"final_text", Result.FinalText},
            {"error", Result.Error}, {"steps", Result.Counters.Steps},
            {"tool_calls", Result.Counters.ToolCalls},
            {"repair_attempts", Result.Counters.RepairAttempts}
        }.dump(2);
        return Result.Status == Pico::EAgentStatus::Completed ? 0 : 2;
    }
    catch (const std::exception& Exception)
    {
        std::cerr << "PicoAgentHost: " << Exception.what() << '\n';
        return 1;
    }
}
