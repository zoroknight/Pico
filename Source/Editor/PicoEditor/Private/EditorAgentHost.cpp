#include "Pico/Editor/EditorAgentHost.h"

#include "Pico/Editor/EditorAgentExecutionService.h"
#include "Pico/Core/Paths.h"
#include "Pico/Tasks/GameThreadDispatcher.h"

#include <imgui.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Pico
{
namespace
{
const char* PermissionDisplayName(EAgentToolPermission Permission)
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

const char* ToolDisplayName(std::string_view Name)
{
    if (Name == "editor.object.set_properties") return "修改对象属性";
    if (Name == "editor.object.batch_set_properties") return "批量修改对象属性";
    if (Name == "editor.actor.spawn") return "创建场景 Actor";
    if (Name == "editor.actor.spawn_blueprint") return "创建 Actor Blueprint 实例";
    if (Name == "editor.actor.delete") return "删除场景 Actor";
    if (Name == "editor.actor.delete_many") return "批量删除场景 Actor";
    if (Name == "editor.agent.revert_run") return "恢复 Agent 操作前的场景";
    if (Name == "editor.scene.create_room") return "创建碰撞房间";
    if (Name == "editor.gameplay.create_third_person_character") return "创建第三人称角色";
    if (Name == "editor.actor.set_location") return "修改 Actor 位置";
    if (Name == "editor.play.start") return "运行当前项目";
    if (Name == "editor.play.stop") return "停止当前运行项目";
    if (Name == "editor.world.save") return "保存当前世界";
    if (Name == "editor.project.create_from_third_person_template") return "创建第三人称项目";
    if (Name == "editor.project.package") return "打包当前项目";
    return "执行 Agent 工具";
}

const char* ToolDescription(std::string_view Name)
{
    if (Name == "editor.object.set_properties") return "Agent 将修改对象的反射属性，可通过 Undo 撤销。";
    if (Name == "editor.object.batch_set_properties") return "Agent 将在一次事务中修改多个对象属性，可整体 Undo。";
    if (Name == "editor.actor.spawn") return "Agent 将在当前世界中创建一个新的 Actor。";
    if (Name == "editor.actor.spawn_blueprint") return "Agent 将使用指定 Actor Blueprint 创建实例。";
    if (Name == "editor.actor.delete") return "Agent 将删除指定 Actor，可通过 Undo 撤销。";
    if (Name == "editor.actor.delete_many") return "Agent 将在一次事务中删除一组 Actor。";
    if (Name == "editor.agent.revert_run") return "Agent 将恢复指定 Run 开始前的 World 快照。";
    if (Name == "editor.play.start") return "Agent 将启动编辑器当前配置的 Play 会话。";
    if (Name == "editor.play.stop") return "Agent 将停止编辑器拥有的 Play 会话。";
    if (Name == "editor.project.package") return "Agent 将生成可分发项目包。";
    return "Agent 请求执行会修改场景、项目文件或进程状态的操作。";
}

class FApprovalBroker final : public IAgentToolApproval
{
public:
    void SetSession(std::string Session)
    {
        std::lock_guard Lock(Mutex);
        CurrentSession = Session.empty() ? "editor-local" : std::move(Session);
    }

    bool RequestApproval(const FAgentToolCall& Call,
        EAgentToolPermission Permission, std::string_view Description) override
    {
        std::unique_lock Lock(Mutex);
        Condition.wait(Lock, [this]() { return !bPending || bShuttingDown; });
        if (bShuttingDown) return false;
        const std::string Scope = CurrentSession;
        if (Permission == EAgentToolPermission::ModifyWorld
            && SessionAllowedTools[Scope].contains(Call.Name))
        {
            return true;
        }
        PendingCall = Call;
        PendingSession = Scope;
        PendingPermission = Permission;
        PendingDescription = Description;
        bHasDecision = false;
        bPending = true;
        Condition.wait(Lock, [this]() { return bHasDecision || bShuttingDown; });
        const bool Result = !bShuttingDown && bApproved;
        bPending = false;
        PendingCall.reset();
        Condition.notify_all();
        return Result;
    }

    void Decide(bool Approved, bool AllowForSession = false)
    {
        std::lock_guard Lock(Mutex);
        if (!bPending) return;
        if (Approved && AllowForSession && PendingCall
            && PendingPermission == EAgentToolPermission::ModifyWorld)
        {
            SessionAllowedTools[PendingSession].insert(PendingCall->Name);
        }
        bApproved = Approved;
        bHasDecision = true;
        Condition.notify_all();
    }

    bool GetPending(FAgentToolCall& Call, EAgentToolPermission& Permission,
        std::string& Description, std::string& Session) const
    {
        std::lock_guard Lock(Mutex);
        if (!bPending || !PendingCall) return false;
        Call = *PendingCall;
        Permission = PendingPermission;
        Description = PendingDescription;
        Session = PendingSession;
        return true;
    }

    void Shutdown()
    {
        std::lock_guard Lock(Mutex);
        bShuttingDown = true;
        bHasDecision = true;
        bApproved = false;
        SessionAllowedTools.clear();
        Condition.notify_all();
    }

private:
    mutable std::mutex Mutex;
    std::condition_variable Condition;
    std::optional<FAgentToolCall> PendingCall;
    std::unordered_map<std::string, std::unordered_set<std::string>> SessionAllowedTools;
    std::string CurrentSession = "editor-local";
    std::string PendingSession;
    std::string PendingDescription;
    EAgentToolPermission PendingPermission = EAgentToolPermission::ReadOnly;
    bool bPending = false;
    bool bHasDecision = false;
    bool bApproved = false;
    bool bShuttingDown = false;
};
}

struct FEditorAgentHost::FImpl
{
    FImpl(FEngineLoop* EngineLoop, FEditorSelection* Selection,
        FEditorTransactionManager* Transactions, FGameThreadDispatcher* Dispatcher,
        std::function<void()> OnWorldChanged, FEditorAgentHostServices HostServices)
        : Tools(EngineLoop, Selection, Transactions, &Approval,
            std::move(OnWorldChanged), std::move(HostServices))
        , Execution(&Tools, Dispatcher,
            FPaths::GetProjectSavedDir() / "Agent/Operations",
            [this](const FAgentToolCall& Call, FAgentToolResult Result,
                const FCancellationToken* Cancellation)
            {
                return Tools.WaitForAsyncCompletion(
                    Call, std::move(Result), Cancellation);
            })
    {
    }

    FApprovalBroker Approval;
    FEditorAgentToolExecutor Tools;
    FEditorAgentExecutionService Execution;
    std::string LastFocusedApprovalId;
    bool bShutdown = false;
};

FEditorAgentHost::FEditorAgentHost(FEngineLoop* EngineLoop,
    FEditorSelection* Selection, FEditorTransactionManager* Transactions,
    FGameThreadDispatcher* Dispatcher, std::function<void()> OnWorldChanged,
    FEditorAgentHostServices HostServices)
    : Impl(std::make_unique<FImpl>(EngineLoop, Selection, Transactions, Dispatcher,
        std::move(OnWorldChanged), std::move(HostServices)))
{
}

FEditorAgentHost::FEditorAgentHost(FEngineLoop* EngineLoop,
    FEditorSelection* Selection, FEditorTransactionManager* Transactions,
    FGameThreadDispatcher* Dispatcher, std::function<void()> OnWorldChanged,
    FEditorCommandService* Commands, FEditorWorldDocument* WorldDocument,
    std::function<std::pair<bool, std::string>(
        const std::filesystem::path&, const std::string&, bool)> StartPackage,
    std::function<FEditorAgentPackageCompletion(
        const FCancellationToken*)> WaitForPackage,
    std::function<std::pair<bool, std::string>()> StartPlay,
    std::function<std::pair<bool, std::string>()> StopPlay,
    FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot)
    : FEditorAgentHost(EngineLoop, Selection, Transactions, Dispatcher,
        std::move(OnWorldChanged),
        FEditorAgentHostServices {Commands, WorldDocument,
            std::move(StartPackage), std::move(WaitForPackage),
            std::move(StartPlay), std::move(StopPlay),
            std::move(RestoreSnapshot),
            FPaths::GetProjectSavedDir() / "Agent/ChangeSets"})
{
}

FEditorAgentHost::~FEditorAgentHost() { Shutdown(); }

FEditorAgentToolExecutor& FEditorAgentHost::GetTools() { return Impl->Tools; }
FEditorAgentExecutionService& FEditorAgentHost::GetExecutionService()
{
    return Impl->Execution;
}

void FEditorAgentHost::ConfigureSession(std::string SessionId,
    EAgentTurnIntent Intent, const std::vector<FAgentSkill>& Skills)
{
    Impl->Approval.SetSession(SessionId);
    Impl->Execution.SetSessionId(std::move(SessionId));
    Impl->Execution.SetTurnIntent(Intent);
    Impl->Execution.SetAllowedTools(Skills);
}

void FEditorAgentHost::CancelPendingApproval() { Impl->Approval.Decide(false); }

void FEditorAgentHost::DrawApprovalCenter()
{
    FAgentToolCall Call;
    EAgentToolPermission Permission;
    std::string Description;
    std::string Session;
    if (!Impl->Approval.GetPending(Call, Permission, Description, Session)) return;
    if (Impl->LastFocusedApprovalId != Call.Id)
    {
        ImGui::SetNextWindowFocus();
        Impl->LastFocusedApprovalId = Call.Id;
    }
    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->GetCenter(), ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.13f, 0.08f, 0.98f));
    if (ImGui::Begin("MCP / Agent 操作审批", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), "需要你的批准");
        ImGui::Separator();
        ImGui::Text("操作：%s", ToolDisplayName(Call.Name));
        ImGui::Text("权限：%s", PermissionDisplayName(Permission));
        ImGui::TextDisabled("会话：%s", Session.c_str());
        ImGui::TextDisabled("工具：%s", Call.Name.c_str());
        ImGui::TextWrapped("说明：%s", ToolDescription(Call.Name));
        ImGui::TextWrapped("参数：%s", Call.ArgumentsJson.c_str());
        ImGui::Separator();
        if (ImGui::Button("批准一次")) Impl->Approval.Decide(true);
        if (Permission == EAgentToolPermission::ModifyWorld)
        {
            ImGui::SameLine();
            if (ImGui::Button("本会话允许此工具")) Impl->Approval.Decide(true, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("拒绝")) Impl->Approval.Decide(false);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void FEditorAgentHost::Shutdown()
{
    if (!Impl || Impl->bShutdown) return;
    Impl->bShutdown = true;
    Impl->Approval.Shutdown();
    Impl->Execution.Shutdown();
}
}
