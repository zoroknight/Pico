#include "Pico/Agent/AgentRuntime.h"

#include "Pico/Tasks/TaskSystem.h"

namespace Pico
{
FAgentRuntime::FAgentRuntime(
    FAgentSession& InSession,
    IAgentProvider& InProvider,
    IAgentToolExecutor& InToolExecutor,
    FAgentBudget InBudget)
    : Session(InSession)
    , Provider(InProvider)
    , ToolExecutor(InToolExecutor)
    , Budget(InBudget)
    , Counters(InSession.GetStatus() == EAgentStatus::Planning
            || InSession.GetStatus() == EAgentStatus::AwaitingApproval
            || InSession.GetStatus() == EAgentStatus::ExecutingTool
            || InSession.GetStatus() == EAgentStatus::Validating
            || InSession.GetStatus() == EAgentStatus::Repairing
        ? InSession.GetCounters()
        : FAgentCounters {})
{
}

FAgentRunResult FAgentRuntime::Run(
    std::string Prompt,
    const FCancellationToken* CancellationToken)
{
    StartTime = std::chrono::steady_clock::now();
    std::string Error;
    if (!Prompt.empty())
    {
        FAgentEvent UserMessage;
        UserMessage.Type = EAgentEventType::Message;
        UserMessage.Role = EAgentRole::User;
        UserMessage.Content = std::move(Prompt);
        if (!Session.Append(std::move(UserMessage), &Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error));
        }
    }

    if (!Transition(EAgentStatus::Planning, Error))
    {
        return Finish(EAgentStatus::Failed, std::move(Error));
    }

    while (true)
    {
        if (IsCancelled(CancellationToken))
        {
            return Finish(EAgentStatus::Cancelled, "Agent run was cancelled");
        }
        if (!CheckBudget(Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error));
        }

        ++Counters.Steps;
        FAgentProviderRequest Request;
        Request.Messages = Session.BuildMessageHistory();
        Request.Step = Counters.Steps;
        Request.RepairAttempt = Counters.RepairAttempts;
        FAgentProviderResponse Response = Provider.Generate(Request, CancellationToken);

        if (IsCancelled(CancellationToken) || Response.Error == "Cancelled")
        {
            return Finish(EAgentStatus::Cancelled, "Agent run was cancelled");
        }
        if (!Response.bSucceeded)
        {
            FAgentEvent Failure;
            Failure.Type = EAgentEventType::Error;
            Failure.Content = Response.Error.empty() ? "Provider failed" : Response.Error;
            Session.Append(std::move(Failure));
            if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
            {
                return Finish(EAgentStatus::Failed, "Agent repair budget exhausted");
            }
            ++Counters.RepairAttempts;
            if (!Transition(EAgentStatus::Repairing, Error)
                || !Session.WriteCheckpoint(EAgentStatus::Repairing, Counters, &Error)
                || !Transition(EAgentStatus::Planning, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
            continue;
        }

        if (!Response.Content.empty())
        {
            FAgentEvent AssistantMessage;
            AssistantMessage.Type = EAgentEventType::Message;
            AssistantMessage.Role = EAgentRole::Assistant;
            AssistantMessage.Content = Response.Content;
            if (!Session.Append(std::move(AssistantMessage), &Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
        }

        if (!Response.ToolCalls.empty())
        {
            bool bNeedsApproval = false;
            std::size_t NewToolCallCount = 0;
            for (const FAgentToolCall& Call : Response.ToolCalls)
            {
                const std::optional<FAgentToolResult> ExistingResult =
                    Session.FindToolResult(Call.Id);
                if (ExistingResult && !Session.MatchesToolCall(Call))
                {
                    return Finish(EAgentStatus::Failed,
                        "ToolCall id was reused with different tool or arguments: " + Call.Id);
                }
                if (!ExistingResult) ++NewToolCallCount;
                bNeedsApproval |= !ExistingResult && ToolExecutor.RequiresApproval(Call);
            }
            if (Counters.ToolCalls > Budget.MaxToolCalls
                || NewToolCallCount > Budget.MaxToolCalls - Counters.ToolCalls)
            {
                return Finish(EAgentStatus::Failed,
                    "Agent tool-call budget exhausted before approval");
            }
            if (bNeedsApproval)
            {
                if (!Transition(EAgentStatus::AwaitingApproval, Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error));
                }
                for (const FAgentToolCall& Call : Response.ToolCalls)
                {
                    if (!Session.FindToolResult(Call.Id)
                        && ToolExecutor.RequiresApproval(Call))
                    {
                        ToolExecutor.PrepareApproval(Call);
                    }
                }
            }
            if (!Transition(EAgentStatus::ExecutingTool, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
            bool bToolFailed = false;
            for (const FAgentToolCall& Call : Response.ToolCalls)
            {
                if (IsCancelled(CancellationToken))
                {
                    return Finish(EAgentStatus::Cancelled, "Agent run was cancelled");
                }
                if (Call.Id.empty() || Call.Name.empty())
                {
                    bToolFailed = true;
                    Error = "Provider emitted a tool call without a stable id or name";
                    break;
                }

                std::optional<FAgentToolResult> Existing = Session.FindToolResult(Call.Id);
                FAgentToolResult Result;
                if (Existing)
                {
                    Result = *Existing;
                    Result.bReused = true;
                }
                else
                {
                    if (Counters.ToolCalls >= Budget.MaxToolCalls)
                    {
                        return Finish(EAgentStatus::Failed, "Agent tool-call budget exhausted");
                    }
                    FAgentEvent CallEvent;
                    CallEvent.Type = EAgentEventType::ToolCall;
                    CallEvent.CallId = Call.Id;
                    CallEvent.ToolName = Call.Name;
                    CallEvent.PayloadJson = Call.ArgumentsJson;
                    if (!Session.Append(std::move(CallEvent), &Error))
                    {
                        return Finish(EAgentStatus::Failed, std::move(Error));
                    }
                    ++Counters.ToolCalls;
                    Result = ToolExecutor.Execute(Call, CancellationToken);
                    Result.CallId = Call.Id;
                }

                FAgentEvent ResultEvent;
                ResultEvent.Type = EAgentEventType::ToolResult;
                ResultEvent.Role = EAgentRole::Tool;
                ResultEvent.CallId = Call.Id;
                ResultEvent.ToolName = Call.Name;
                ResultEvent.PayloadJson = Result.OutputJson;
                ResultEvent.TraceJson = ToolExecutor.GetLastExecutionTraceJson();
                ResultEvent.Content = Result.Error;
                ResultEvent.bSucceeded = Result.bSucceeded;
                ResultEvent.bReused = Result.bReused;
                if (!Session.Append(std::move(ResultEvent), &Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error));
                }
                if (!Result.bSucceeded)
                {
                    bToolFailed = true;
                    Error = Result.Error.empty() ? "Tool execution failed" : Result.Error;
                    break;
                }
            }

            if (bToolFailed)
            {
                if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
                {
                    return Finish(EAgentStatus::Failed, Error);
                }
                ++Counters.RepairAttempts;
                if (!Transition(EAgentStatus::Repairing, Error)
                    || !Session.WriteCheckpoint(EAgentStatus::Repairing, Counters, &Error)
                    || !Transition(EAgentStatus::Planning, Error))
                {
                    return Finish(EAgentStatus::Failed, std::move(Error));
                }
                continue;
            }

            if (!Transition(EAgentStatus::Validating, Error)
                || !Session.WriteCheckpoint(EAgentStatus::Validating, Counters, &Error)
                || !Transition(EAgentStatus::Planning, Error))
            {
                return Finish(EAgentStatus::Failed, std::move(Error));
            }
            continue;
        }

        if (Response.bFinal)
        {
            FAgentRunResult Result = Finish(EAgentStatus::Completed);
            Result.FinalText = Response.Content;
            return Result;
        }

        if (Counters.RepairAttempts >= Budget.MaxRepairAttempts)
        {
            return Finish(EAgentStatus::Failed, "Provider returned neither a final answer nor a tool call");
        }
        ++Counters.RepairAttempts;
        if (!Transition(EAgentStatus::Repairing, Error)
            || !Session.WriteCheckpoint(EAgentStatus::Repairing, Counters, &Error)
            || !Transition(EAgentStatus::Planning, Error))
        {
            return Finish(EAgentStatus::Failed, std::move(Error));
        }
    }
}

bool FAgentRuntime::IsCancelled(const FCancellationToken* CancellationToken) const
{
    return CancellationToken && CancellationToken->IsCancellationRequested();
}

bool FAgentRuntime::CheckBudget(std::string& OutError) const
{
    if (Counters.Steps >= Budget.MaxSteps)
    {
        OutError = "Agent step budget exhausted";
        return false;
    }
    const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - StartTime).count();
    if (Elapsed >= static_cast<std::int64_t>(Budget.MaxElapsedMilliseconds))
    {
        OutError = "Agent elapsed-time budget exhausted";
        return false;
    }
    return true;
}

bool FAgentRuntime::Transition(EAgentStatus Status, std::string& OutError)
{
    if (!IsAllowedAgentTransition(Session.GetStatus(), Status))
    {
        OutError = "Invalid Agent state transition from "
            + std::string(ToString(Session.GetStatus())) + " to "
            + std::string(ToString(Status));
        return false;
    }
    return Session.SetStatus(Status, &OutError);
}

FAgentRunResult FAgentRuntime::Finish(EAgentStatus Status, std::string Error)
{
    std::string PersistenceError;
    if (!Error.empty())
    {
        FAgentEvent Event;
        Event.Type = EAgentEventType::Error;
        Event.Content = Error;
        Session.Append(std::move(Event), &PersistenceError);
    }
    Session.SetStatus(Status, &PersistenceError);
    Session.WriteCheckpoint(Status, Counters, &PersistenceError);
    if (!PersistenceError.empty() && Error.empty()) Error = PersistenceError;
    return {Status, {}, std::move(Error), Counters};
}
}
