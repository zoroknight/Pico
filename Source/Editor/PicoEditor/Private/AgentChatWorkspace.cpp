#include "AgentChatWorkspace.h"
#include "MarkdownRenderer.h"

#include "Pico/Agent/AgentRuntime.h"
#include "Pico/Agent/AgentCredentialStore.h"
#include "Pico/Agent/AgentIntent.h"
#include "Pico/Agent/AgentKnowledgeStore.h"
#include "Pico/Agent/AgentOperationJournal.h"
#include "Pico/Agent/AgentProjectHandoff.h"
#include "Pico/Agent/AgentSkill.h"
#include "Pico/Agent/FakeAgentProvider.h"
#include "Pico/Agent/OpenAICompatibleProvider.h"
#include "Pico/Core/Config.h"
#include "Pico/Core/Paths.h"
#include "Pico/Editor/EditorAgentTools.h"
#include "Pico/Tasks/GameThreadDispatcher.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
using FJson = nlohmann::json;

enum class EChatProvider
{
    Fake,
    DeepSeek,
    Kimi
};

const char* ApprovalPermissionDisplayName(EAgentToolPermission Permission)
{
    switch (Permission)
    {
    case EAgentToolPermission::ReadOnly: return "只读访问";
    case EAgentToolPermission::ModifyWorld: return "修改当前场景";
    case EAgentToolPermission::WriteProject: return "写入项目文件";
    case EAgentToolPermission::LaunchProcess: return "启动或停止外部进程";
    }
    return "未知权限";
}

const char* ApprovalToolDisplayName(std::string_view ToolName)
{
    if (ToolName == "editor.object.set_properties") return "修改对象属性";
    if (ToolName == "editor.object.batch_set_properties") return "批量修改对象属性";
    if (ToolName == "editor.actor.spawn") return "创建场景 Actor";
    if (ToolName == "editor.actor.spawn_blueprint") return "创建 Actor Blueprint 实例";
    if (ToolName == "editor.actor.delete") return "删除场景 Actor";
    if (ToolName == "editor.actor.delete_many") return "批量删除场景 Actor";
    if (ToolName == "editor.agent.revert_run") return "恢复 Agent 操作前的场景";
    if (ToolName == "editor.scene.create_room") return "创建碰撞房间";
    if (ToolName == "editor.gameplay.create_third_person_character") return "创建第三人称角色";
    if (ToolName == "editor.actor.set_location") return "修改 Actor 位置";
    if (ToolName == "editor.play.start") return "运行当前项目";
    if (ToolName == "editor.play.stop") return "停止当前运行项目";
    if (ToolName == "editor.world.save") return "保存当前世界";
    if (ToolName == "editor.project.create_from_third_person_template") return "创建第三人称项目";
    if (ToolName == "editor.project.package") return "打包当前项目";
    return "执行 Agent 工具";
}

const char* ApprovalToolDescription(std::string_view ToolName)
{
    if (ToolName == "editor.object.set_properties") return "Agent 将修改对象的反射属性，可通过 Undo 撤销。";
    if (ToolName == "editor.object.batch_set_properties") return "Agent 将在一次事务中修改多个对象的反射属性，可整体 Undo。";
    if (ToolName == "editor.actor.spawn") return "Agent 将在当前世界中创建一个新的 Actor。";
    if (ToolName == "editor.actor.spawn_blueprint") return "Agent 将使用指定 Actor Blueprint 在当前世界创建实例。";
    if (ToolName == "editor.actor.delete") return "Agent 将从当前世界删除指定 Actor，可通过 Undo 撤销。";
    if (ToolName == "editor.actor.delete_many") return "Agent 将在一次事务中删除一组已验证的 Actor，可整体 Undo。";
    if (ToolName == "editor.agent.revert_run") return "Agent 将恢复指定 Run 开始前的完整 World 快照；若场景之后又被修改，会拒绝覆盖。";
    if (ToolName == "editor.scene.create_room") return "Agent 将在一次事务中创建地板和碰撞墙壁。";
    if (ToolName == "editor.gameplay.create_third_person_character") return "Agent 将配置可操控角色、PlayerStart 和第三人称 Gameplay 链。";
    if (ToolName == "editor.actor.set_location") return "Agent 将修改指定 Actor 的世界坐标，可通过 Undo 撤销。";
    if (ToolName == "editor.play.start") return "Agent 将启动编辑器当前配置的 Play 会话，并保持运行直到你停止。";
    if (ToolName == "editor.play.stop") return "Agent 将停止编辑器拥有的 Play 会话及其进程。";
    if (ToolName == "editor.world.save") return "Agent 将把当前世界保存到现有项目资产路径。";
    if (ToolName == "editor.project.create_from_third_person_template") return "Agent 将复制模板内容并创建一个新的 Pico 项目目录。";
    if (ToolName == "editor.project.package") return "Agent 将在指定目录生成可分发项目包，可能需要较长时间。";
    return "Agent 请求执行一个会修改场景、项目文件或进程状态的操作。";
}

const char* ProviderDisplayName(EChatProvider Provider)
{
    switch (Provider)
    {
    case EChatProvider::Fake: return "Fake Scene Agent";
    case EChatProvider::DeepSeek: return "DeepSeek";
    case EChatProvider::Kimi: return "Kimi";
    }
    return "Unknown";
}

const char* ProviderSessionSlug(EChatProvider Provider)
{
    switch (Provider)
    {
    case EChatProvider::Fake: return "fake";
    case EChatProvider::DeepSeek: return "deepseek";
    case EChatProvider::Kimi: return "kimi";
    }
    return "unknown";
}

const char* DefaultModelForProvider(EChatProvider Provider)
{
    switch (Provider)
    {
    case EChatProvider::DeepSeek: return "deepseek-v4-flash";
    case EChatProvider::Kimi: return "kimi-k2.6";
    case EChatProvider::Fake: return "offline-fake";
    }
    return "offline-fake";
}

std::optional<EChatProvider> ParseProviderSessionSlug(std::string_view Provider)
{
    if (Provider == "fake") return EChatProvider::Fake;
    if (Provider == "deepseek") return EChatProvider::DeepSeek;
    if (Provider == "kimi") return EChatProvider::Kimi;
    return std::nullopt;
}

const char* CredentialProviderId(EChatProvider Provider)
{
    switch (Provider)
    {
    case EChatProvider::DeepSeek: return "DeepSeek";
    case EChatProvider::Kimi: return "Kimi";
    case EChatProvider::Fake: return "";
    }
    return "";
}

const char* CredentialEnvironmentName(EChatProvider Provider)
{
    switch (Provider)
    {
    case EChatProvider::DeepSeek: return "DEEPSEEK_API_KEY";
    case EChatProvider::Kimi: return "MOONSHOT_API_KEY";
    case EChatProvider::Fake: return "";
    }
    return "";
}

void ClearSecret(std::string& Text)
{
    volatile char* Data = Text.empty() ? nullptr : Text.data();
    for (std::size_t Index = 0; Index < Text.size(); ++Index)
        Data[Index] = 0;
    Text.clear();
}

struct FChatToolEntry
{
    std::string CallId;
    std::string ToolName;
    std::string ArgumentsJson = "{}";
    std::string ResultMarkdown;
    bool bHasResult = false;
    bool bSucceeded = false;
    bool bReused = false;
};

struct FChatLine
{
    std::string Label;
    std::string Text;
    ImVec4 Color {0.82f, 0.84f, 0.88f, 1.0f};
    bool bToolSummary = false;
    std::vector<FChatToolEntry> Tools;
};

std::string JsonCodeBlock(std::string_view JsonText)
{
    std::string Formatted(JsonText);
    try
    {
        Formatted = FJson::parse(JsonText).dump(2);
    }
    catch (const std::exception&)
    {
        // Tool errors may deliberately carry plain text instead of JSON.
    }
    return "```json\n" + Formatted + "\n```";
}

std::string ToolResultMarkdown(const FAgentEvent& Event)
{
    std::string Text;
    if (Event.StructuredResultJson != "{}")
        Text = JsonCodeBlock(Event.StructuredResultJson);
    else
        Text = Event.bSucceeded ? JsonCodeBlock(Event.PayloadJson) : Event.Content;
    if (Event.bReused) Text += "\n\n*Result reused by CallId*";
    if (Event.TraceJson != "[]")
        Text += "\n\n**Execution trace**\n\n" + JsonCodeBlock(Event.TraceJson);
    return Text;
}

struct FChatSessionEntry
{
    std::string Id;
    std::string DisplayName;
    std::filesystem::path Path;
    std::filesystem::file_time_type LastWriteTime {};
};

std::string MakeChatSessionId(EChatProvider Provider)
{
    static std::atomic<std::uint64_t> Sequence {1};
    const auto Now = std::chrono::system_clock::now();
    const std::time_t Time = std::chrono::system_clock::to_time_t(Now);
    std::tm LocalTime {};
#ifdef _WIN32
    localtime_s(&LocalTime, &Time);
#else
    localtime_r(&Time, &LocalTime);
#endif
    std::ostringstream Stream;
    Stream << "editor-chat-" << ProviderSessionSlug(Provider) << '-'
           << std::put_time(&LocalTime, "%Y%m%d-%H%M%S") << '-'
           << Sequence.fetch_add(1);
    return Stream.str();
}

std::string SessionDisplayName(
    const std::string& SessionId,
    EChatProvider Provider)
{
    const std::string Prefix = std::string("editor-chat-")
        + ProviderSessionSlug(Provider);
    if (SessionId == Prefix) return "Legacy conversation";
    if (SessionId.size() > Prefix.size() + 1)
        return SessionId.substr(Prefix.size() + 1);
    return SessionId;
}

std::string WrapSelectableText(std::string_view Text, float Width)
{
    if (Text.empty() || Width <= 1.0f) return std::string(Text);
    ImFont* Font = ImGui::GetFont();
    const float Scale = ImGui::GetFontSize() / Font->FontSize;
    std::string Result;
    const char* Cursor = Text.data();
    const char* End = Cursor + Text.size();
    while (Cursor < End)
    {
        const char* NewLine = static_cast<const char*>(
            std::memchr(Cursor, '\n', static_cast<std::size_t>(End - Cursor)));
        const char* ParagraphEnd = NewLine ? NewLine : End;
        while (Cursor < ParagraphEnd)
        {
            const char* Wrap = Font->CalcWordWrapPositionA(
                Scale, Cursor, ParagraphEnd, Width);
            if (Wrap <= Cursor) Wrap = ParagraphEnd;
            Result.append(Cursor, Wrap);
            Cursor = Wrap;
            while (Cursor < ParagraphEnd && (*Cursor == ' ' || *Cursor == '\t'))
                ++Cursor;
            if (Cursor < ParagraphEnd) Result.push_back('\n');
        }
        if (NewLine)
        {
            Result.push_back('\n');
            Cursor = NewLine + 1;
        }
    }
    return Result;
}

bool CopyIconButton(const char* Id, const char* Tooltip)
{
    const float Size = ImGui::GetFrameHeight();
    const ImVec2 Position = ImGui::GetCursorScreenPos();
    const bool bPressed = ImGui::InvisibleButton(Id, ImVec2(Size, Size));
    const ImU32 Color = ImGui::GetColorU32(
        ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const float Unit = Size * 0.22f;
    DrawList->AddRect(
        ImVec2(Position.x + Unit * 1.35f, Position.y + Unit * 0.75f),
        ImVec2(Position.x + Size - Unit * 0.75f, Position.y + Size - Unit * 1.35f),
        Color, 1.0f, 0, 1.5f);
    DrawList->AddRect(
        ImVec2(Position.x + Unit * 0.75f, Position.y + Unit * 1.35f),
        ImVec2(Position.x + Size - Unit * 1.35f, Position.y + Size - Unit * 0.75f),
        Color, 1.0f, 0, 1.5f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tooltip);
    return bPressed;
}

std::string BuildToolSummaryText(const std::vector<FChatToolEntry>& Tools)
{
    std::size_t Succeeded = 0;
    std::size_t Failed = 0;
    std::size_t Pending = 0;
    for (const FChatToolEntry& Tool : Tools)
    {
        if (!Tool.bHasResult) ++Pending;
        else if (Tool.bSucceeded) ++Succeeded;
        else ++Failed;
    }
    std::ostringstream Text;
    Text << "工具调用摘要：共 " << Tools.size() << " 个，成功 "
         << Succeeded << "，失败 " << Failed;
    if (Pending > 0) Text << "，等待 " << Pending;
    Text << "。\n\n| # | 工具 | 结果 |\n|---:|---|---|\n";
    for (std::size_t Index = 0; Index < Tools.size(); ++Index)
    {
        const FChatToolEntry& Tool = Tools[Index];
        const char* Status = !Tool.bHasResult ? "等待"
            : Tool.bSucceeded ? (Tool.bReused ? "成功（复用）" : "成功")
                              : "失败";
        Text << "| " << Index + 1 << " | `" << Tool.ToolName
             << "` | " << Status << " |\n";
    }
    return Text.str();
}

void DrawToolSummary(const FChatLine& Line)
{
    std::size_t Succeeded = 0;
    std::size_t Failed = 0;
    for (const FChatToolEntry& Tool : Line.Tools)
    {
        Succeeded += Tool.bHasResult && Tool.bSucceeded ? 1U : 0U;
        Failed += Tool.bHasResult && !Tool.bSucceeded ? 1U : 0U;
    }
    ImGui::TextColored(Line.Color, "工具调用摘要");
    const float CopyButtonX = std::max(ImGui::GetCursorPosX() + 8.0f,
        ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::SameLine(CopyButtonX);
    if (CopyIconButton("##CopyToolSummary", "复制工具调用摘要"))
        ImGui::SetClipboardText(Line.Text.c_str());

    const std::string Header = "调用 " + std::to_string(Line.Tools.size())
        + " 个工具 | 成功 " + std::to_string(Succeeded)
        + " | 失败 " + std::to_string(Failed) + "##ToolSummary";
    if (!ImGui::CollapsingHeader(Header.c_str())) return;

    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_Borders
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##ToolSummaryTable", 3, Flags))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("工具");
        ImGui::TableSetupColumn("结果", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableHeadersRow();
        for (std::size_t Index = 0; Index < Line.Tools.size(); ++Index)
        {
            const FChatToolEntry& Tool = Line.Tools[Index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", Index + 1);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(Tool.ToolName.c_str());
            ImGui::TableSetColumnIndex(2);
            if (!Tool.bHasResult) ImGui::TextDisabled("等待");
            else if (Tool.bSucceeded)
                ImGui::TextColored(ImVec4(0.42f, 0.88f, 0.55f, 1.0f),
                    "%s", Tool.bReused ? "成功/复用" : "成功");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f), "失败");
        }
        ImGui::EndTable();
    }
    for (std::size_t Index = 0; Index < Line.Tools.size(); ++Index)
    {
        const FChatToolEntry& Tool = Line.Tools[Index];
        ImGui::PushID(static_cast<int>(Index));
        const std::string DetailLabel = std::to_string(Index + 1) + ". "
            + Tool.ToolName + "##ToolDetail";
        if (ImGui::TreeNode(DetailLabel.c_str()))
        {
            ImGui::TextDisabled("参数");
            DrawMarkdown(JsonCodeBlock(Tool.ArgumentsJson));
            if (Tool.bHasResult)
            {
                ImGui::TextDisabled("结果与执行轨迹");
                DrawMarkdown(Tool.ResultMarkdown);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

class FInteractiveApproval final : public IAgentToolApproval
{
public:
    bool RequestApproval(
        const FAgentToolCall& Call,
        EAgentToolPermission Permission,
        std::string_view Description) override
    {
        std::unique_lock Lock(Mutex);
        PendingCall = Call;
        PendingPermission = Permission;
        PendingDescription = Description;
        bHasDecision = false;
        bPending = true;
        Condition.notify_all();
        Condition.wait(Lock, [this]() { return bHasDecision || bShuttingDown; });
        const bool bResult = !bShuttingDown && bApproved;
        bPending = false;
        PendingCall.reset();
        return bResult;
    }

    void Decide(bool bInApproved)
    {
        std::lock_guard Lock(Mutex);
        if (!bPending) return;
        bApproved = bInApproved;
        bHasDecision = true;
        Condition.notify_all();
    }

    void CancelWait()
    {
        Decide(false);
    }

    void Shutdown()
    {
        std::lock_guard Lock(Mutex);
        bShuttingDown = true;
        bHasDecision = true;
        bApproved = false;
        Condition.notify_all();
    }

    bool GetPending(
        FAgentToolCall& OutCall,
        EAgentToolPermission& OutPermission,
        std::string& OutDescription) const
    {
        std::lock_guard Lock(Mutex);
        if (!bPending || !PendingCall) return false;
        OutCall = *PendingCall;
        OutPermission = PendingPermission;
        OutDescription = PendingDescription;
        return true;
    }

private:
    mutable std::mutex Mutex;
    std::condition_variable Condition;
    std::optional<FAgentToolCall> PendingCall;
    EAgentToolPermission PendingPermission = EAgentToolPermission::ReadOnly;
    std::string PendingDescription;
    bool bPending = false;
    bool bHasDecision = false;
    bool bApproved = false;
    bool bShuttingDown = false;
};

class FGameThreadToolExecutor final : public IAgentToolExecutor
{
public:
    FGameThreadToolExecutor(
        FEditorAgentToolExecutor* InEditorTools,
        FGameThreadDispatcher* InDispatcher,
        std::filesystem::path OperationDirectory)
        : EditorTools(InEditorTools), Dispatcher(InDispatcher)
        , Journal(std::move(OperationDirectory))
    {
    }

    void BeginRun(std::string_view RunId) override
    {
        DispatchRunLifecycle(std::string(RunId), EAgentStatus::Planning, true);
    }

    void EndRun(std::string_view RunId, EAgentStatus Status) override
    {
        DispatchRunLifecycle(std::string(RunId), Status, false);
    }

    bool RequiresApproval(const FAgentToolCall& Call) const override
    {
        if (!IntentError(Call).empty()) return false;
        return EditorTools && EditorTools->RequiresApproval(Call);
    }

    bool IsReadOnly(const FAgentToolCall& Call) const override
    {
        return EditorTools && EditorTools->IsReadOnly(Call);
    }

    std::vector<std::string> GetRevisionReadSet(
        const FAgentToolCall& Call) const override
    {
        return EditorTools ? EditorTools->GetRevisionReadSet(Call)
            : std::vector<std::string>{"State.Revision"};
    }

    std::vector<std::string> GetRevisionWriteSet(
        const FAgentToolCall& Call) const override
    {
        return EditorTools ? EditorTools->GetRevisionWriteSet(Call)
            : std::vector<std::string>{"State.Revision"};
    }

    void PrepareApproval(const FAgentToolCall& Call) override
    {
        if (!IntentError(Call).empty()) return;
        if (EditorTools) EditorTools->PrepareApproval(Call);
    }

    void SetTurnIntent(EAgentTurnIntent InIntent)
    {
        TurnIntent.store(InIntent);
    }

    void SetAllowedTools(const std::vector<FAgentSkill>& Skills)
    {
        std::lock_guard Lock(SkillMutex);
        bSkillRestricted = !Skills.empty();
        AllowedTools.clear();
        for (const FAgentSkill& Skill : Skills)
            AllowedTools.insert(
                Skill.AllowedTools.begin(), Skill.AllowedTools.end());
    }

    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) override
    {
        (void)CancellationToken;
        LastTraceJson = "[]";
        const std::string BlockedReason = IntentError(Call);
        if (!BlockedReason.empty())
        {
            LastTraceJson = FailureTrace("Intent", BlockedReason);
            return {Call.Id, false, "{}", BlockedReason, false};
        }
        const bool bDurable = !IsReadOnly(Call);
        if (bDurable)
        {
            std::string JournalError;
            if (const auto Recovered = Journal.FindApplied(Call, &JournalError))
            {
                LastTraceJson = FJson::array({{{"stage", "Recovery"},
                    {"succeeded", true},
                    {"message", "Recovered the previously applied tool result; handler was not called again"}}}).dump();
                return *Recovered;
            }
            if (!JournalError.empty()
                || !Journal.Prepare(Call, &JournalError)
                || !Journal.MarkExecuting(Call, &JournalError))
            {
                LastTraceJson = FailureTrace("Journal", JournalError);
                return {Call.Id, false, "{}",
                    "Could not prepare durable operation: " + JournalError, false};
            }
        }
        struct FSharedResult
        {
            std::mutex Mutex;
            std::condition_variable Condition;
            FAgentToolResult Result;
            bool bDone = false;
        };
        auto Shared = std::make_shared<FSharedResult>();
        FEditorAgentToolExecutor* Tools = EditorTools;
        if (!Tools || !Dispatcher || Dispatcher->Post(
                "Execute Agent editor tool",
                [Shared, Tools, Call]()
                {
                    FAgentToolResult Result = Tools->Execute(Call, nullptr);
                    {
                        std::lock_guard Lock(Shared->Mutex);
                        Shared->Result = std::move(Result);
                        Shared->bDone = true;
                    }
                    Shared->Condition.notify_all();
                }) == 0)
        {
            LastTraceJson = FailureTrace(
                "Execute", "Game Thread dispatcher is unavailable");
            return {Call.Id, false, "{}", "Game Thread dispatcher is unavailable", false};
        }

        std::unique_lock Lock(Shared->Mutex);
        while (!Shared->bDone)
        {
            Shared->Condition.wait_for(Lock, std::chrono::milliseconds(10));
        }
        LastTraceJson = Tools->GetLastExecutionTraceJson();
        FAgentToolResult Result = Shared->Result;
        if (Result.bSucceeded)
        {
            Result = Tools->WaitForAsyncCompletion(
                Call, std::move(Result), CancellationToken);
            if (!Result.bSucceeded)
            {
                LastTraceJson = FailureTrace(
                    "AsyncCompletion",
                    Result.Error.empty()
                        ? "Asynchronous tool operation failed" : Result.Error);
            }
        }
        if (bDurable)
        {
            std::string JournalError;
            if (!Journal.MarkApplied(Call, Result, &JournalError))
            {
                LastTraceJson = FailureTrace("Journal", JournalError);
                return {Call.Id, false, "{}",
                    "Tool returned, but its durable result could not be recorded; outcome may be uncertain: "
                        + JournalError,
                    false};
            }
        }
        return Result;
    }

    void CommitDurableResult(const FAgentToolCall& Call) override
    {
        if (IsReadOnly(Call)) return;
        std::string Error;
        Journal.MarkCommitted(Call, &Error);
    }

    std::vector<FAgentOperationRecord> ListIncompleteOperations() const
    {
        return Journal.ListIncomplete();
    }

    std::string GetLastExecutionTraceJson() const override
    {
        return LastTraceJson;
    }

private:
    void DispatchRunLifecycle(
        std::string RunId,
        EAgentStatus Status,
        bool bBegin)
    {
        struct FCompletion
        {
            std::mutex Mutex;
            std::condition_variable Condition;
            bool bDone = false;
        };
        auto Completion = std::make_shared<FCompletion>();
        FEditorAgentToolExecutor* Tools = EditorTools;
        if (!Tools || !Dispatcher || Dispatcher->Post(
                bBegin ? "Begin Agent Run ChangeSet" : "End Agent Run ChangeSet",
                [Completion, Tools, RunId = std::move(RunId), Status, bBegin]()
                {
                    if (bBegin) Tools->BeginRun(RunId);
                    else Tools->EndRun(RunId, Status);
                    {
                        std::lock_guard Lock(Completion->Mutex);
                        Completion->bDone = true;
                    }
                    Completion->Condition.notify_all();
                }) == 0)
            return;
        std::unique_lock Lock(Completion->Mutex);
        while (!Completion->bDone)
            Completion->Condition.wait_for(Lock, std::chrono::milliseconds(10));
    }

    static std::string FailureTrace(
        std::string_view Stage,
        std::string_view Message)
    {
        return FJson::array({{{"stage", Stage}, {"succeeded", false},
            {"message", Message}}}).dump();
    }

    std::string IntentError(const FAgentToolCall& Call) const
    {
        {
            std::lock_guard Lock(SkillMutex);
            if (bSkillRestricted && !AllowedTools.contains(Call.Name))
                return "The active Pico Skill does not allow tool '" + Call.Name + "'";
        }
        const EAgentTurnIntent Intent = TurnIntent.load();
        if (Intent == EAgentTurnIntent::Play
            && Call.Name == "editor.project.package")
        {
            return "This turn requests Play, not packaging. Do not package the project; call editor.play.start instead.";
        }
        if (Intent == EAgentTurnIntent::Package
            && Call.Name == "editor.play.start")
        {
            return "This turn requests packaging, not Play. Do not start a Play Session; call editor.project.package instead.";
        }
        return {};
    }

    FEditorAgentToolExecutor* EditorTools = nullptr;
    FGameThreadDispatcher* Dispatcher = nullptr;
    FAgentOperationJournal Journal;
    std::string LastTraceJson = "[]";
    std::atomic<EAgentTurnIntent> TurnIntent {EAgentTurnIntent::General};
    mutable std::mutex SkillMutex;
    std::unordered_set<std::string> AllowedTools;
    bool bSkillRestricted = false;
};

class FFakeSceneAgentProvider final : public IAgentProvider
{
public:
    explicit FFakeSceneAgentProvider(std::string InNonce)
        : Nonce(std::move(InNonce))
    {
    }

    FAgentProviderResponse Generate(
        const FAgentProviderRequest& Request,
        const FCancellationToken*) override
    {
        std::size_t LastUser = 0;
        for (std::size_t Index = 0; Index < Request.Messages.size(); ++Index)
            if (Request.Messages[Index].Role == EAgentRole::User) LastUser = Index;
        std::vector<const FAgentMessage*> ToolResults;
        for (std::size_t Index = LastUser + 1; Index < Request.Messages.size(); ++Index)
            if (Request.Messages[Index].Role == EAgentRole::Tool)
                ToolResults.push_back(&Request.Messages[Index]);

        FAgentProviderResponse Response;
        if (ToolResults.empty())
        {
            Response.Content = "I will inspect the active World first.";
            Response.ToolCalls.push_back(
                {"fake-describe-" + Nonce, "editor.world.describe", "{}"});
        }
        else if (ToolResults.size() == 1)
        {
            Response.Content = "The World is available. I will create one Cube Actor.";
            Response.ToolCalls.push_back({"fake-spawn-" + Nonce, "editor.actor.spawn",
                FJson {{"name", "AI_Cube_" + Nonce}, {"kind", "Cube"}}.dump()});
        }
        else if (ToolResults.size() == 2)
        {
            try
            {
                const std::string ObjectPath =
                    FJson::parse(ToolResults.back()->Content).at("object_path").get<std::string>();
                Response.Content = "The Cube exists. I will place it above the ground.";
                Response.ToolCalls.push_back({"fake-move-" + Nonce,
                    "editor.actor.set_location", FJson {{"object_path", ObjectPath},
                        {"x", 150.0}, {"y", 0.0}, {"z", 100.0}}.dump()});
            }
            catch (const std::exception& Exception)
            {
                return {false, false, {},
                    "Fake scene agent could not read spawn result: "
                        + std::string(Exception.what()), {}};
            }
        }
        else
        {
            Response.bFinal = true;
            Response.Content = "Scene task completed: inspected the World, created a Cube, and moved it to (150, 0, 100).";
        }
        return Response;
    }

private:
    std::string Nonce;
};

std::string EnvironmentValue(const char* Name)
{
#ifdef _WIN32
    char* Value = nullptr;
    std::size_t Length = 0;
    if (_dupenv_s(&Value, &Length, Name) != 0 || Value == nullptr)
        return {};
    std::string Result(Value);
    std::free(Value);
    return Result;
#else
    const char* Value = std::getenv(Name);
    return Value ? Value : "";
#endif
}

std::string MakeRunNonce()
{
    static std::atomic<std::uint64_t> Sequence {1};
    const auto Value = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(Value) + "_" + std::to_string(Sequence.fetch_add(1));
}
}

struct FAgentChatWorkspace::FImpl
{
    FImpl(
        FEngineLoop* InEngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        FTaskSystem* InTaskSystem,
        FGameThreadDispatcher* InDispatcher,
        std::function<void()> OnWorldChanged,
        FEditorCommandService* Commands,
        FEditorWorldDocument* WorldDocument,
        std::function<std::pair<bool, std::string>(
            const std::filesystem::path&, const std::string&, bool)> StartPackage,
        std::function<FEditorAgentPackageCompletion(
            const FCancellationToken*)> WaitForPackage,
        std::function<std::pair<bool, std::string>()> StartPlay,
        std::function<std::pair<bool, std::string>()> StopPlay,
        FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot,
        std::function<void(const std::filesystem::path&)> InRequestProjectOpen)
        : EngineLoop(InEngineLoop)
        , TaskSystem(InTaskSystem)
        , Dispatcher(InDispatcher)
        , KnowledgeStore(FPaths::GetProjectSavedDir() / "Agent/Knowledge")
        , EditorTools(InEngineLoop, Selection, Transactions, &Approval,
            std::move(OnWorldChanged),
            {Commands, WorldDocument, std::move(StartPackage),
                std::move(WaitForPackage),
                std::move(StartPlay), std::move(StopPlay),
                std::move(RestoreSnapshot),
                FPaths::GetProjectSavedDir() / "Agent/ChangeSets"})
        , GameThreadTools(&EditorTools, InDispatcher,
            FPaths::GetProjectSavedDir() / "Agent/Operations")
        , RequestProjectOpen(std::move(InRequestProjectOpen))
    {
        std::snprintf(Model.data(), Model.size(), "%s", "offline-fake");
        LoadChatPreferences();
        std::string StartupError;
        if (!KnowledgeStore.Load(&StartupError)) Status = StartupError;
        std::string SkillError;
        if (!SkillRegistry.LoadDirectory(
                FPaths::GetEngineRootDir() / "Config/Agent/Skills",
                EditorTools.GetToolNames(), &SkillError))
        {
            Status = SkillError;
        }
        std::string HandoffError;
        const std::optional<FAgentProjectHandoff> Handoff =
            ConsumeAgentProjectHandoff(FPaths::GetProjectRootDir(), &HandoffError);
        if (Handoff)
        {
            const std::optional<EChatProvider> HandoffProvider =
                ParseProviderSessionSlug(Handoff->Provider);
            if (HandoffProvider)
            {
                Provider = *HandoffProvider;
                SessionId = Handoff->SessionId;
                if (!Handoff->Model.empty())
                    std::snprintf(Model.data(), Model.size(), "%s",
                        Handoff->Model.c_str());
                SaveChatPreferences();
                Status = "Project handoff restored this conversation";
            }
            else
            {
                HandoffError = "Project handoff used an unknown Provider";
            }
        }
        RefreshSessionList(!Handoff.has_value());
        RefreshCredentialState();
        RefreshSessionView();
        if (!HandoffError.empty()) Status = HandoffError;
        else if (Handoff) Status = "Project handoff restored this conversation";
    }

    void RefreshSessionList(bool bSelectLatest)
    {
        SessionEntries.clear();
        SessionDirectory.clear();
        FPaths::TryGetProjectWritePath(
            EProjectWriteRoot::Saved,
            std::filesystem::path("Agent/Sessions"), SessionDirectory);
        std::error_code Error;
        std::filesystem::create_directories(SessionDirectory, Error);
        const std::string Prefix = std::string("editor-chat-")
            + ProviderSessionSlug(Provider);
        if (!Error)
        {
            for (const auto& Entry :
                std::filesystem::directory_iterator(SessionDirectory, Error))
            {
                if (Error || !Entry.is_regular_file() ||
                    Entry.path().extension() != ".jsonl")
                    continue;
                const std::string Id = Entry.path().stem().string();
                if (Id != Prefix && Id.rfind(Prefix + '-', 0) != 0) continue;
                SessionEntries.push_back({Id, SessionDisplayName(Id, Provider),
                    Entry.path(), Entry.last_write_time(Error)});
                Error.clear();
            }
        }
        std::sort(SessionEntries.begin(), SessionEntries.end(),
            [](const FChatSessionEntry& Left, const FChatSessionEntry& Right)
            {
                return Left.LastWriteTime > Right.LastWriteTime;
            });

        const auto Existing = std::find_if(
            SessionEntries.begin(), SessionEntries.end(),
            [this](const FChatSessionEntry& Entry)
            {
                return Entry.Id == SessionId;
            });
        if (!bSelectLatest && Existing != SessionEntries.end())
        {
            SessionPath = Existing->Path;
            return;
        }
        if (!SessionEntries.empty())
        {
            SessionId = SessionEntries.front().Id;
            SessionPath = SessionEntries.front().Path;
            return;
        }
        SessionId = MakeChatSessionId(Provider);
        SessionPath = SessionDirectory / (SessionId + ".jsonl");
    }

    void CreateNewSession()
    {
        if (bRunning.load()) return;
        SessionId = MakeChatSessionId(Provider);
        SessionPath = SessionDirectory / (SessionId + ".jsonl");
        std::string Error;
        auto Session = FAgentSession::OpenOrCreate(SessionId, SessionPath, &Error);
        if (!Session)
        {
            Status = "Could not create conversation: " + Error;
            return;
        }
        RefreshSessionList(false);
        RefreshSessionView();
    }

    void DeleteCurrentSession()
    {
        if (bRunning.load() || SessionPath.empty()) return;
        std::error_code Error;
        std::filesystem::remove(SessionPath, Error);
        if (Error)
        {
            Status = "Could not delete conversation: " + Error.message();
            return;
        }
        SessionId.clear();
        SessionPath.clear();
        RefreshSessionList(true);
        RefreshSessionView();
    }

    void RefreshCredentialState()
    {
        bStoredCredential = false;
        CredentialError.clear();
        const char* ProviderId = CredentialProviderId(Provider);
        if (ProviderId[0] == '\0') return;
        std::string ApiKey;
        bStoredCredential = CredentialStore.TryLoadApiKey(
            ProviderId, ApiKey, &CredentialError);
        ClearSecret(ApiKey);
    }

    std::filesystem::path GetChatSettingsPath() const
    {
        const std::filesystem::path& EngineRoot = FPaths::GetEngineRootDir();
        return EngineRoot.empty() ? std::filesystem::path {}
            : EngineRoot / "Saved/Editor/Agent/ChatSettings.ini";
    }

    void LoadChatPreferences()
    {
        const std::filesystem::path Path = GetChatSettingsPath();
        FConfigFile Config;
        if (Path.empty() || !Config.Load(Path)) return;
        const std::optional<EChatProvider> SavedProvider =
            ParseProviderSessionSlug(
                Config.GetString("Chat", "LastProvider", "fake"));
        if (SavedProvider) Provider = *SavedProvider;
        const std::string SavedModel = Config.GetString("Models",
            ProviderSessionSlug(Provider), DefaultModelForProvider(Provider));
        std::snprintf(Model.data(), Model.size(), "%s", SavedModel.c_str());
    }

    void SaveChatPreferences()
    {
        const std::filesystem::path Path = GetChatSettingsPath();
        if (Path.empty()) return;
        FConfigFile Config;
        std::error_code FileError;
        if (std::filesystem::exists(Path, FileError) && !FileError)
            Config.Load(Path);
        Config.SetString("Chat", "LastProvider", ProviderSessionSlug(Provider));
        Config.SetString("Models", ProviderSessionSlug(Provider), Model.data());
        if (!Config.Save(Path))
            PreferenceError = "Could not save editor-local AI Chat preferences";
        else
            PreferenceError.clear();
    }

    void RefreshSessionView()
    {
        if (SessionPath.empty()) return;
        std::string Error;
        auto Session = FAgentSession::OpenOrCreate(SessionId, SessionPath, &Error);
        if (!Session)
        {
            std::lock_guard Lock(ViewMutex);
            Status = "Session load failed: " + Error;
            return;
        }
        std::vector<FChatLine> NewLines;
        const std::vector<FAgentEvent>& Events = Session->GetEvents();
        std::size_t Index = 0;
        while (Index < Events.size())
        {
            const FAgentEvent& Event = Events[Index];
            if (Event.Type == EAgentEventType::Message
                && Event.Role == EAgentRole::User)
            {
                NewLines.push_back({"You", Event.Content,
                    ImVec4(0.45f, 0.78f, 1.0f, 1.0f)});
                std::size_t End = Index + 1;
                while (End < Events.size()
                    && !(Events[End].Type == EAgentEventType::Message
                        && Events[End].Role == EAgentRole::User))
                    ++End;

                std::string AssistantText;
                std::vector<std::pair<std::string, std::size_t>> Errors;
                std::vector<FChatToolEntry> Tools;
                std::unordered_map<std::string, std::size_t> ToolIndices;
                for (std::size_t Cursor = Index + 1; Cursor < End; ++Cursor)
                {
                    const FAgentEvent& RunEvent = Events[Cursor];
                    if (RunEvent.Type == EAgentEventType::Message
                        && RunEvent.Role == EAgentRole::Assistant
                        && !RunEvent.Content.empty())
                    {
                        if (!AssistantText.empty()) AssistantText += "\n\n";
                        AssistantText += RunEvent.Content;
                    }
                    else if (RunEvent.Type == EAgentEventType::ToolCall)
                    {
                        ToolIndices[RunEvent.CallId] = Tools.size();
                        Tools.push_back({RunEvent.CallId, RunEvent.ToolName,
                            RunEvent.PayloadJson});
                    }
                    else if (RunEvent.Type == EAgentEventType::ToolResult)
                    {
                        auto Tool = ToolIndices.find(RunEvent.CallId);
                        if (Tool == ToolIndices.end())
                        {
                            ToolIndices[RunEvent.CallId] = Tools.size();
                            Tools.push_back({RunEvent.CallId, RunEvent.ToolName});
                            Tool = ToolIndices.find(RunEvent.CallId);
                        }
                        FChatToolEntry& Entry = Tools[Tool->second];
                        if (Entry.ToolName.empty()) Entry.ToolName = RunEvent.ToolName;
                        Entry.ResultMarkdown = ToolResultMarkdown(RunEvent);
                        Entry.bHasResult = true;
                        Entry.bSucceeded = RunEvent.bSucceeded;
                        Entry.bReused = RunEvent.bReused;
                    }
                    else if (RunEvent.Type == EAgentEventType::Error
                        && !RunEvent.Content.empty())
                    {
                        auto Existing = std::find_if(Errors.begin(), Errors.end(),
                            [&RunEvent](const auto& Item)
                            {
                                return Item.first == RunEvent.Content;
                            });
                        if (Existing == Errors.end())
                            Errors.emplace_back(RunEvent.Content, 1U);
                        else
                            ++Existing->second;
                    }
                }
                if (!AssistantText.empty())
                {
                    NewLines.push_back({"Assistant", std::move(AssistantText),
                        ImVec4(0.72f, 0.90f, 0.74f, 1.0f)});
                }
                if (!Errors.empty())
                {
                    std::string ErrorText;
                    for (const auto& [Message, Count] : Errors)
                    {
                        if (!ErrorText.empty()) ErrorText += "\n\n";
                        ErrorText += Message;
                        if (Count > 1)
                            ErrorText += "\n\n（相同错误重复 "
                                + std::to_string(Count) + " 次）";
                    }
                    NewLines.push_back({"Error", std::move(ErrorText),
                        ImVec4(1.0f, 0.42f, 0.36f, 1.0f)});
                }
                if (!Tools.empty())
                {
                    FChatLine Summary;
                    Summary.Label = "工具调用摘要";
                    Summary.Text = BuildToolSummaryText(Tools);
                    Summary.Color = ImVec4(0.68f, 0.84f, 0.72f, 1.0f);
                    Summary.bToolSummary = true;
                    Summary.Tools = std::move(Tools);
                    NewLines.push_back(std::move(Summary));
                }
                Index = End;
                continue;
            }
            if (Event.Type == EAgentEventType::Message)
            {
                const bool bUser = Event.Role == EAgentRole::User;
                NewLines.push_back({bUser ? "You" : "Assistant", Event.Content,
                    bUser ? ImVec4(0.45f, 0.78f, 1.0f, 1.0f)
                          : ImVec4(0.72f, 0.90f, 0.74f, 1.0f)});
            }
            else if (Event.Type == EAgentEventType::Error)
            {
                NewLines.push_back({"Error", Event.Content,
                    ImVec4(1.0f, 0.42f, 0.36f, 1.0f)});
            }
            ++Index;
        }
        std::lock_guard Lock(ViewMutex);
        Lines = std::move(NewLines);
        Status = std::string(ToString(Session->GetStatus()));
        bScrollToBottom.store(true);
    }

    std::unique_ptr<IAgentProvider> CreateProvider(
        EChatProvider ProviderType,
        std::string ModelName,
        std::string ToolCatalogJson,
        std::string& OutError)
    {
        if (ProviderType == EChatProvider::Fake)
            return std::make_unique<FFakeSceneAgentProvider>(MakeRunNonce());

        FOpenAICompatibleProviderSettings Settings;
        Settings.Model = std::move(ModelName);
        Settings.ToolCatalogJson = std::move(ToolCatalogJson);
        Settings.SystemPrompt =
            "You are the Pico Editor scene assistant. Use only the provided tools. "
            "Inspect before modifying, make the smallest requested change, and report the result. "
            "Use editor.world.describe for live World Actors and their locations; asset search finds project assets, not Actor instances. "
            "Do not repeat a read-only query when its result cannot provide the missing information, and execute once the requested tool arguments are known. "
            "When the user requests an Actor Blueprint instance or additional character/NPC, use editor.actor.spawn_blueprint; never substitute a Cube. "
            "When the user asks to run, play, preview, or launch the active project, use editor.play.start; never substitute validation or packaging. "
            "Use editor.play.stop only when the user explicitly asks to stop the running Play Session. "
            "Use editor.project.package only when the user explicitly asks to package, build, or export a distributable project. "
            "Use editor.gameplay.create_third_person_character only when authoring the unique playable Player 0 Pawn and PlayerStart. "
            "For scene assembly, search assets before referencing them, create structural room geometry before gameplay Actors, then validate and save before packaging. "
            "After creating a project from the third-person template, finish the current answer concisely; Pico will open a clean editor process and restore this conversation in the new project. "
            "Before editing reflected properties, call editor.object.describe and use the exact component object path, property name, current compound value, units, semantic, and range it returns. "
            "Use plain Markdown without Emoji; the editor deliberately omits unsupported color Emoji. "
            "Do not repeat raw tool arguments, Tool Results, or execution traces in assistant prose; the editor provides one expandable tool summary after the turn. "
            "Never invent object paths or claim a tool succeeded before receiving its result.";
        Settings.TimeoutMilliseconds = static_cast<std::uint32_t>(TimeoutSeconds * 1000);
        Settings.MaxRetries = static_cast<std::size_t>(MaxRetries);
        if (ProviderType == EChatProvider::DeepSeek)
        {
            Settings.Endpoint = "https://api.deepseek.com/chat/completions";
            Settings.ApiKey = EnvironmentValue("DEEPSEEK_API_KEY");
            Settings.bSendThinkingSetting = true;
            Settings.bThinkingEnabled = false;
        }
        else
        {
            Settings.Endpoint = "https://api.moonshot.cn/v1/chat/completions";
            Settings.ApiKey = EnvironmentValue("MOONSHOT_API_KEY");
        }
        if (Settings.ApiKey.empty())
        {
            std::string LocalCredentialError;
            CredentialStore.TryLoadApiKey(
                CredentialProviderId(ProviderType), Settings.ApiKey,
                &LocalCredentialError);
            if (Settings.ApiKey.empty())
            {
                OutError = !LocalCredentialError.empty()
                    ? LocalCredentialError
                    : std::string(CredentialEnvironmentName(ProviderType))
                        + " is not set and no API key is saved";
            }
        }
        if (!OutError.empty()) return {};
        auto Transport = CreatePlatformAgentHttpTransport();
        if (!Transport)
        {
            OutError = "No platform HTTPS transport is available";
            return {};
        }
        return std::make_unique<FOpenAICompatibleProvider>(
            std::move(Settings), std::move(Transport));
    }

    std::string RefreshKnowledge(
        const std::string& Prompt,
        std::vector<FAgentKnowledgeHit>& OutHits,
        std::string& OutError)
    {
        OutError.clear();
        std::map<std::string, std::vector<FAgentKnowledgeRecord>> Sources;
        Sources["world"] = {};
        Sources["assets"] = {};
        Sources["selection"] = {};
        Sources["message-log"] = {};
        Sources["tool-schema"] = {};
        Sources["project-file"] = CollectProjectTextKnowledge(
            FPaths::GetProjectRootDir(), 64 * 1024, 64);
        for (FAgentKnowledgeRecord& Record : EditorTools.CollectKnowledgeRecords())
            Sources[Record.SourceType].push_back(std::move(Record));

        FAgentKnowledgeRecord ToolRecord;
        ToolRecord.SourcePath = "AgentToolRegistry";
        ToolRecord.Title = "Available Pico Agent tools and JSON schemas";
        ToolRecord.Content = EditorTools.BuildToolCatalogJson();
        ToolRecord.Tags = {"agent", "tool", "schema", "reflection"};
        ToolRecord.Provenance = "Live AgentToolRegistry catalog";
        Sources["tool-schema"].push_back(std::move(ToolRecord));

        for (auto& [SourceType, Records] : Sources)
            if (!KnowledgeStore.ReplaceSource(
                    SourceType, std::move(Records), &OutError))
                return "{}";

        FAgentKnowledgeQuery Query;
        Query.Text = Prompt;
        Query.MaxResults = 8;
        Query.MaxContextBytes = 12000;
        return KnowledgeStore.BuildGroundingContextJson(Query, &OutHits);
    }

    void Send()
    {
        if (!TaskSystem || bRunning.load() || Input[0] == '\0') return;
        const std::string Prompt = Input.data();
        Input.fill('\0');
        GameThreadTools.SetTurnIntent(ClassifyAgentTurnIntent(Prompt));
        const EChatProvider SelectedProvider = Provider;
        const std::string SelectedModel = Model.data();
        const std::string SelectedProviderName =
            ProviderDisplayName(SelectedProvider);
        const std::string SelectedSessionId = SessionId;
        const std::filesystem::path SelectedSessionPath = SessionPath;
        std::vector<FAgentKnowledgeHit> KnowledgeHits;
        std::string KnowledgeError;
        const std::string KnowledgeContext = RefreshKnowledge(
            Prompt, KnowledgeHits, KnowledgeError);
        if (!KnowledgeError.empty())
        {
            std::lock_guard Lock(ViewMutex);
            Status = KnowledgeError;
            Lines.push_back({"Error", KnowledgeError,
                ImVec4(1.0f, 0.42f, 0.36f, 1.0f)});
            return;
        }
        const std::vector<FAgentSkill> ActiveSkills = SkillRegistry.Select(Prompt);
        GameThreadTools.SetAllowedTools(ActiveSkills);
        const std::string SkillContext =
            SkillRegistry.BuildSkillContextJson(ActiveSkills);
        const std::string ToolCatalog = SkillRegistry.FilterToolCatalogJson(
            EditorTools.BuildToolCatalogJson(), ActiveSkills);
        std::string ProviderError;
        std::unique_ptr<IAgentProvider> NewProvider = CreateProvider(
            SelectedProvider, SelectedModel, ToolCatalog, ProviderError);
        if (!NewProvider)
        {
            std::lock_guard Lock(ViewMutex);
            Status = ProviderError;
            Lines.push_back({"Error", ProviderError, ImVec4(1.0f, 0.42f, 0.36f, 1.0f)});
            return;
        }
        {
            std::lock_guard Lock(ViewMutex);
            Status = "Planning with " + SelectedProviderName + " / "
                + SelectedModel;
            Lines.push_back({"You", Prompt, ImVec4(0.45f, 0.78f, 1.0f, 1.0f)});
            StreamingText.clear();
            LastKnowledgeHits = KnowledgeHits;
            LastActiveSkillIds.clear();
            for (const FAgentSkill& Skill : ActiveSkills)
                LastActiveSkillIds.push_back(Skill.Id + "@" + Skill.Version);
        }
        bRunning.store(true);
        std::shared_ptr<IAgentProvider> SharedProvider(std::move(NewProvider));
        ActiveTask = TaskSystem->Submit(
            "Pico Agent chat turn",
            [this, Prompt, AgentProvider = std::move(SharedProvider),
                SelectedProvider, SelectedProviderName, SelectedModel, SelectedSessionId,
                SelectedSessionPath, KnowledgeContext, SkillContext](
                const FCancellationToken& Token) mutable
            {
                std::string Error;
                std::optional<std::filesystem::path> ProjectToOpen;
                auto Session = FAgentSession::OpenOrCreate(
                    SelectedSessionId, SelectedSessionPath, &Error);
                FAgentRunResult Result;
                if (Session)
                {
                    const std::size_t FirstNewEvent = Session->GetEvents().size();
                    FAgentBudget Budget;
                    Budget.MaxSteps = 12;
                    Budget.MaxToolCalls = 16;
                    Budget.MaxReadOnlyToolCalls = 6;
                    Budget.MaxMutationToolCalls = 10;
                    Budget.MaxConsecutiveNoProgressSteps = 2;
                    Budget.ReservedFinalSteps = 1;
                    Budget.MaxRepairAttempts = 2;
                    Budget.MaxElapsedMilliseconds = 120000;
                    FAgentRuntimeContext RuntimeContext;
                    RuntimeContext.KnowledgeContextJson = KnowledgeContext;
                    RuntimeContext.SkillContextJson = SkillContext;
                    RuntimeContext.OnAssistantDelta = [this](std::string_view Delta)
                    {
                        std::lock_guard Lock(ViewMutex);
                        StreamingText.append(Delta);
                        bScrollToBottom.store(true);
                    };
                    FAgentRuntime Runtime(*Session, *AgentProvider,
                        GameThreadTools, Budget, std::move(RuntimeContext));
                    Result = Runtime.Run(Prompt, &Token);
                    if (Result.Status == EAgentStatus::Completed)
                    {
                        std::optional<std::filesystem::path> CreatedProject;
                        const auto& Events = Session->GetEvents();
                        for (std::size_t Index = FirstNewEvent;
                             Index < Events.size(); ++Index)
                        {
                            const FAgentEvent& Event = Events[Index];
                            if (Event.Type != EAgentEventType::ToolResult
                                || !Event.bSucceeded
                                || Event.ToolName
                                    != "editor.project.create_from_third_person_template")
                                continue;
                            try
                            {
                                CreatedProject = FJson::parse(Event.PayloadJson)
                                    .at("project_file").get<std::string>();
                            }
                            catch (...) { CreatedProject.reset(); }
                        }
                        if (CreatedProject)
                        {
                            FAgentProjectHandoff Handoff;
                            Handoff.SourceProjectFile = FPaths::GetProjectFile();
                            Handoff.TargetProjectFile = *CreatedProject;
                            Handoff.SessionPath = SelectedSessionPath;
                            Handoff.SessionId = SelectedSessionId;
                            Handoff.Provider = ProviderSessionSlug(SelectedProvider);
                            Handoff.Model = SelectedModel;
                            Handoff.Goal = Prompt;
                            std::string HandoffError;
                            if (PrepareAgentProjectHandoff(Handoff, &HandoffError))
                            {
                                ProjectToOpen = *CreatedProject;
                            }
                            else
                            {
                                Result.Error = "Project was created, but editor handoff failed: "
                                    + HandoffError;
                            }
                        }
                    }
                }
                else
                {
                    Result.Status = EAgentStatus::Failed;
                    Result.Error = Error;
                }
                {
                    std::lock_guard Lock(ViewMutex);
                    StreamingText.clear();
                }
                RefreshSessionView();
                {
                    std::lock_guard Lock(ViewMutex);
                    Status = SelectedProviderName + " / " + SelectedModel
                        + ": " + std::string(ToString(Result.Status));
                    if (!Result.Error.empty()) Status += ": " + Result.Error;
                    LastRunCounters = Result.Counters;
                    LastRunContextBytes = Result.ContextBytes;
                    LastRunId = Result.RunId;
                }
                bRunning.store(false);
                if (ProjectToOpen && Dispatcher && RequestProjectOpen)
                {
                    const auto OpenProject = RequestProjectOpen;
                    const std::filesystem::path ProjectFile = *ProjectToOpen;
                    Dispatcher->Post("Open Agent-created project",
                        [OpenProject, ProjectFile]() { OpenProject(ProjectFile); });
                }
            });
        if (!ActiveTask || !ActiveTask->IsValid())
        {
            bRunning.store(false);
            std::lock_guard Lock(ViewMutex);
            Status = "Task system rejected the Agent run";
        }
    }

    void Cancel()
    {
        Approval.CancelWait();
        if (ActiveTask) ActiveTask->RequestCancel();
    }

    void Draw(bool* Open)
    {
        ImGui::SetNextWindowSize(ImVec2(620.0f, 720.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("AI Chat", Open))
        {
            ImGui::End();
            return;
        }

        const char* ProviderNames[] = {"Fake Scene Agent", "DeepSeek", "Kimi"};
        int ProviderIndex = static_cast<int>(Provider);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::BeginDisabled(bRunning.load());
        if (ImGui::Combo("Provider", &ProviderIndex, ProviderNames, 3))
        {
            Provider = static_cast<EChatProvider>(ProviderIndex);
            FConfigFile Config;
            std::string ModelName = DefaultModelForProvider(Provider);
            if (Config.Load(GetChatSettingsPath()))
            {
                ModelName = Config.GetString("Models",
                    ProviderSessionSlug(Provider), ModelName);
            }
            std::snprintf(Model.data(), Model.size(), "%s", ModelName.c_str());
            SaveChatPreferences();
            ApiKeyInput.fill('\0');
            SessionId.clear();
            SessionPath.clear();
            RefreshSessionList(true);
            RefreshCredentialState();
            RefreshSessionView();
        }
        ImGui::EndDisabled();
        if (Provider != EChatProvider::Fake)
        {
            ImGui::SameLine();
            std::string EnvironmentKey = EnvironmentValue(
                CredentialEnvironmentName(Provider));
            const bool bHasEnvironmentKey = !EnvironmentKey.empty();
            ClearSecret(EnvironmentKey);
            const bool bHasKey = bHasEnvironmentKey || bStoredCredential;
            ImGui::TextColored(
                bHasKey ? ImVec4(0.35f, 0.85f, 0.52f, 1.0f)
                        : ImVec4(1.0f, 0.48f, 0.34f, 1.0f),
                bHasEnvironmentKey ? "Environment override"
                    : (bStoredCredential ? "API key saved locally" : "API key missing"));
            ImGui::SetNextItemWidth(240.0f);
            ImGui::BeginDisabled(bRunning.load());
            ImGui::InputText("Model", Model.data(), Model.size());
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveChatPreferences();
            ImGui::EndDisabled();

            if (ImGui::CollapsingHeader(
                    "API Key (Editor Local)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##AgentApiKey", "Paste a new API key",
                    ApiKeyInput.data(), ApiKeyInput.size(),
                    ImGuiInputTextFlags_Password);
                ImGui::BeginDisabled(bRunning.load() || ApiKeyInput[0] == '\0');
                if (ImGui::Button("Save / Replace Key"))
                {
                    std::string ApiKey(ApiKeyInput.data());
                    std::string Error;
                    const bool bSaved = CredentialStore.SaveApiKey(
                        CredentialProviderId(Provider), ApiKey, &Error);
                    ClearSecret(ApiKey);
                    ApiKeyInput.fill('\0');
                    RefreshCredentialState();
                    std::lock_guard Lock(ViewMutex);
                    Status = bSaved
                        ? std::string(CredentialProviderId(Provider))
                            + " API key saved for this local Pico editor"
                        : Error;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(bRunning.load() || !bStoredCredential);
                if (ImGui::Button("Remove Saved Key"))
                {
                    std::string Error;
                    const bool bRemoved = CredentialStore.DeleteApiKey(
                        CredentialProviderId(Provider), &Error);
                    ApiKeyInput.fill('\0');
                    RefreshCredentialState();
                    std::lock_guard Lock(ViewMutex);
                    Status = bRemoved ? "Saved API key removed" : Error;
                }
                ImGui::EndDisabled();
                if (!CredentialError.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f),
                        "%s", CredentialError.c_str());
                }
                const std::string StoragePath =
                    CredentialStore.GetStoragePath().string();
                ImGui::TextDisabled(
                    "Shared by all projects opened from this editor checkout.");
                ImGui::TextDisabled(
                    "Plaintext local secret; hidden from Agent tools, ignored by Git, and excluded from packages.");
                ImGui::TextWrapped("File: %s", StoragePath.c_str());
            }
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Conversation");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-150.0f);
        const std::string CurrentSessionName =
            SessionDisplayName(SessionId, Provider);
        ImGui::BeginDisabled(bRunning.load());
        if (ImGui::BeginCombo("##AgentConversation", CurrentSessionName.c_str()))
        {
            for (const FChatSessionEntry& Entry : SessionEntries)
            {
                const bool bSelected = Entry.Id == SessionId;
                if (ImGui::Selectable(Entry.DisplayName.c_str(), bSelected))
                {
                    SessionId = Entry.Id;
                    SessionPath = Entry.Path;
                    RefreshSessionView();
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Chat")) CreateNewSession();
        ImGui::SameLine();
        ImGui::BeginDisabled(SessionEntries.empty());
        if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete Conversation?");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal(
                "Delete Conversation?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(
                "Delete conversation '%s'? This cannot be undone.",
                CurrentSessionName.c_str());
            if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f)))
            {
                DeleteCurrentSession();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::TextDisabled("Provider: %s | File: %s",
            ProviderDisplayName(Provider), SessionPath.filename().string().c_str());
        if (!PreferenceError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f),
                "%s", PreferenceError.c_str());
        }
        if (ImGui::CollapsingHeader("Request Settings"))
        {
            ImGui::SliderInt("Timeout (seconds)", &TimeoutSeconds, 5, 120);
            ImGui::SliderInt("Retries", &MaxRetries, 0, 3);
            ImGui::TextDisabled(
                "Environment variables override editor-local Saved/Editor/Agent/ApiKeys.ini values.");
        }

        std::string CurrentStatus;
        std::vector<FChatLine> CurrentLines;
        std::string CurrentStreamingText;
        std::vector<FAgentKnowledgeHit> CurrentKnowledgeHits;
        std::vector<std::string> CurrentSkillIds;
        {
            std::lock_guard Lock(ViewMutex);
            CurrentStatus = Status;
            CurrentLines = Lines;
            CurrentStreamingText = StreamingText;
            CurrentKnowledgeHits = LastKnowledgeHits;
            CurrentSkillIds = LastActiveSkillIds;
        }
        if (ImGui::CollapsingHeader("Grounding & Skills"))
        {
            ImGui::Text("Knowledge records: %zu | Retrieved: %zu | Skills: %zu",
                KnowledgeStore.GetRecordCount(), CurrentKnowledgeHits.size(),
                CurrentSkillIds.size());
            ImGui::TextWrapped("Store: %s",
                KnowledgeStore.GetDirectory().string().c_str());
            for (const std::string& Skill : CurrentSkillIds)
                ImGui::BulletText("Skill %s", Skill.c_str());
            for (const FAgentKnowledgeHit& Hit : CurrentKnowledgeHits)
                ImGui::BulletText("[K:%s] %.1f  %s",
                    Hit.Record.Id.c_str(), Hit.Score, Hit.Record.Title.c_str());
        }
        if (ImGui::CollapsingHeader("Agent Metrics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            FAgentCounters MetricsCounters;
            std::uint64_t MetricsContextBytes = 0;
            std::string MetricsRunId;
            {
                std::lock_guard Lock(ViewMutex);
                MetricsCounters = LastRunCounters;
                MetricsContextBytes = LastRunContextBytes;
                MetricsRunId = LastRunId;
            }
            ImGui::Text("Steps %zu | tools %zu | cache hits %zu",
                MetricsCounters.Steps, MetricsCounters.ToolCalls,
                MetricsCounters.SemanticCacheHits);
            ImGui::Text("Context messages %zu | trimmed %zu | cumulative %.1f KB",
                MetricsCounters.ContextMessages,
                MetricsCounters.TrimmedContextMessages,
                static_cast<double>(MetricsContextBytes) / 1024.0);
            if (!MetricsRunId.empty()) ImGui::TextDisabled("Run: %s", MetricsRunId.c_str());
        }
        const std::vector<FAgentOperationRecord> IncompleteOperations =
            GameThreadTools.ListIncompleteOperations();
        if (!IncompleteOperations.empty()
            && ImGui::CollapsingHeader(
                "Agent Recovery", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f),
                "检测到 %zu 个未完成提交的工具操作。", IncompleteOperations.size());
            ImGui::TextWrapped(
                "Applied 操作会在恢复会话时复用结果；Prepared/Executing 操作会由幂等工具边界安全重试。");
            for (const FAgentOperationRecord& Record : IncompleteOperations)
            {
                ImGui::BulletText("%s | %s | %s",
                    Record.ToolName.c_str(), ToString(Record.State).data(),
                    Record.OperationId.c_str());
            }
        }
        ImGui::Separator();
        ImGui::Text("Status: %s", CurrentStatus.c_str());

        FAgentToolCall PendingCall;
        EAgentToolPermission PendingPermission;
        std::string PendingDescription;
        if (Approval.GetPending(PendingCall, PendingPermission, PendingDescription))
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.17f, 0.08f, 1.0f));
            ImGui::BeginChild("AgentApproval", ImVec2(0.0f, 190.0f), true);
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), "需要你的批准");
            ImGui::Text("操作：%s", ApprovalToolDisplayName(PendingCall.Name));
            ImGui::Text("权限：%s", ApprovalPermissionDisplayName(PendingPermission));
            ImGui::TextDisabled("工具：%s", PendingCall.Name.c_str());
            ImGui::TextWrapped("说明：%s", ApprovalToolDescription(PendingCall.Name));
            ImGui::TextWrapped("参数：%s", PendingCall.ArgumentsJson.c_str());
            if (ImGui::Button("批准")) Approval.Decide(true);
            ImGui::SameLine();
            if (ImGui::Button("拒绝")) Approval.Decide(false);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        const float ComposerHeight = 118.0f;
        ImGui::BeginChild("AgentConversation", ImVec2(0.0f,
            -ComposerHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        for (std::size_t Index = 0; Index < CurrentLines.size(); ++Index)
        {
            const FChatLine& Line = CurrentLines[Index];
            ImGui::PushID(static_cast<int>(Index));
            const bool bUserMessage = Line.Label == "You";
            const bool bAssistantMessage = Line.Label == "Assistant";
            const bool bErrorMessage = Line.Label == "Error";
            const bool bMessagePanel = bUserMessage || bAssistantMessage
                || bErrorMessage;
            if (bMessagePanel && Index > 0)
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
            if (Line.bToolSummary)
            {
                DrawToolSummary(Line);
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::PopID();
                continue;
            }

            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 MessagePanelMin {};
            if (bMessagePanel)
            {
                MessagePanelMin = ImGui::GetCursorScreenPos();
                DrawList->ChannelsSplit(2);
                DrawList->ChannelsSetCurrent(1);
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::Indent(12.0f);
            }
            ImGui::TextColored(Line.Color, "%s", Line.Label.c_str());
            const float CopyButtonX = std::max(ImGui::GetCursorPosX() + 8.0f,
                ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
            ImGui::SameLine(CopyButtonX);
            if (CopyIconButton("##CopyMessage", "Copy this message"))
                ImGui::SetClipboardText(Line.Text.c_str());

            if (SelectableMessageIndex && *SelectableMessageIndex == Index)
            {
                const float TextWidth = std::max(
                    80.0f, ImGui::GetContentRegionAvail().x - 8.0f);
                const std::string DisplayText = MakeMarkdownDisplayText(Line.Text);
                std::string WrappedText = WrapSelectableText(DisplayText, TextWidth);
                std::vector<char> Buffer(WrappedText.begin(), WrappedText.end());
                Buffer.push_back('\0');
                const std::size_t LineCount = static_cast<std::size_t>(
                    std::count(WrappedText.begin(), WrappedText.end(), '\n')) + 1;
                const float TextHeight = std::max(
                    ImGui::GetTextLineHeightWithSpacing() + 8.0f,
                    LineCount * ImGui::GetTextLineHeightWithSpacing() + 8.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg,
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f));
                ImGui::InputTextMultiline("##SelectableMessage", Buffer.data(),
                    Buffer.size(), ImVec2(-1.0f, TextHeight),
                    ImGuiInputTextFlags_ReadOnly |
                        ImGuiInputTextFlags_NoHorizontalScroll);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    SelectableMessageIndex.reset();
            }
            else if (DrawMarkdown(Line.Text))
            {
                SelectableMessageIndex = Index;
            }
            if (bMessagePanel)
            {
                ImGui::Unindent(12.0f);
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                const ImVec2 MessagePanelMax(
                    ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x,
                    ImGui::GetCursorScreenPos().y);
                DrawList->ChannelsSetCurrent(0);
                const ImU32 PanelColor = bUserMessage
                    ? IM_COL32(39, 51, 66, 210)
                    : bErrorMessage ? IM_COL32(76, 42, 43, 225)
                                    : IM_COL32(45, 70, 53, 225);
                const ImU32 AccentColor = bUserMessage
                    ? IM_COL32(86, 156, 214, 255)
                    : bErrorMessage ? IM_COL32(226, 92, 86, 255)
                                    : IM_COL32(104, 200, 130, 255);
                DrawList->AddRectFilled(MessagePanelMin, MessagePanelMax,
                    PanelColor, 4.0f);
                DrawList->AddRectFilled(MessagePanelMin,
                    ImVec2(MessagePanelMin.x + 3.0f, MessagePanelMax.y),
                    AccentColor, 4.0f);
                DrawList->ChannelsMerge();
            }
            else
            {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            }
            ImGui::PopID();
        }
        if (!CurrentStreamingText.empty())
        {
            ImGui::PushID("StreamingAssistant");
            ImGui::TextColored(ImVec4(0.72f, 0.90f, 0.74f, 1.0f),
                "Assistant (streaming)");
            const std::string DisplayStreamingText =
                MakeMarkdownDisplayText(CurrentStreamingText);
            ImGui::TextWrapped("%s", DisplayStreamingText.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        if (bScrollToBottom.exchange(false)) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::InputTextMultiline("##AgentPrompt", Input.data(), Input.size(),
            ImVec2(-1.0f, 70.0f));
        const bool bBusy = bRunning.load();
        ImGui::BeginDisabled(bBusy || Input[0] == '\0');
        if (ImGui::Button("Send"))
        {
            Send();
            bScrollToBottom.store(true);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!bBusy);
        if (ImGui::Button("Cancel")) Cancel();
        ImGui::EndDisabled();
        ImGui::End();
    }

    void Shutdown()
    {
        if (bShutdown.exchange(true)) return;
        Approval.Shutdown();
        if (ActiveTask) ActiveTask->RequestCancel();
    }

    FEngineLoop* EngineLoop = nullptr;
    FTaskSystem* TaskSystem = nullptr;
    FGameThreadDispatcher* Dispatcher = nullptr;
    FInteractiveApproval Approval;
    FAgentCredentialStore CredentialStore;
    FAgentKnowledgeStore KnowledgeStore;
    FAgentSkillRegistry SkillRegistry;
    FEditorAgentToolExecutor EditorTools;
    FGameThreadToolExecutor GameThreadTools;
    std::optional<FTaskHandle> ActiveTask;
    std::string SessionId;
    std::filesystem::path SessionPath;
    std::filesystem::path SessionDirectory;
    std::vector<FChatSessionEntry> SessionEntries;
    std::optional<std::size_t> SelectableMessageIndex;
    std::mutex ViewMutex;
    std::vector<FChatLine> Lines;
    std::string StreamingText;
    std::vector<FAgentKnowledgeHit> LastKnowledgeHits;
    std::vector<std::string> LastActiveSkillIds;
    std::string Status = "Idle";
    FAgentCounters LastRunCounters;
    std::uint64_t LastRunContextBytes = 0;
    std::string LastRunId;
    std::array<char, 2048> Input {};
    std::array<char, 128> Model {};
    std::array<char, 640> ApiKeyInput {};
    EChatProvider Provider = EChatProvider::Fake;
    std::atomic<bool> bRunning {false};
    std::atomic<bool> bShutdown {false};
    int TimeoutSeconds = 30;
    int MaxRetries = 2;
    std::atomic<bool> bScrollToBottom {true};
    bool bStoredCredential = false;
    std::string CredentialError;
    std::string PreferenceError;
    std::function<void(const std::filesystem::path&)> RequestProjectOpen;
};

FAgentChatWorkspace::FAgentChatWorkspace(
    FEngineLoop* EngineLoop,
    FEditorSelection* Selection,
    FEditorTransactionManager* Transactions,
    FTaskSystem* TaskSystem,
    FGameThreadDispatcher* Dispatcher,
    std::function<void()> OnWorldChanged,
    FEditorCommandService* Commands,
    FEditorWorldDocument* WorldDocument,
    std::function<std::pair<bool, std::string>(
        const std::filesystem::path&, const std::string&, bool)> StartPackage,
    std::function<FEditorAgentPackageCompletion(
        const FCancellationToken*)> WaitForPackage,
    std::function<std::pair<bool, std::string>()> StartPlay,
    std::function<std::pair<bool, std::string>()> StopPlay,
    FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot,
    std::function<void(const std::filesystem::path&)> RequestProjectOpen)
    : Impl(std::make_unique<FImpl>(EngineLoop, Selection, Transactions,
        TaskSystem, Dispatcher, std::move(OnWorldChanged), Commands,
        WorldDocument, std::move(StartPackage), std::move(WaitForPackage),
        std::move(StartPlay),
        std::move(StopPlay), std::move(RestoreSnapshot),
        std::move(RequestProjectOpen)))
{
}

FAgentChatWorkspace::~FAgentChatWorkspace()
{
    Shutdown();
}

void FAgentChatWorkspace::Draw(bool* Open)
{
    if (Impl) Impl->Draw(Open);
}

void FAgentChatWorkspace::Shutdown()
{
    if (Impl) Impl->Shutdown();
}
}
