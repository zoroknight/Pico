#include "AgentChatWorkspace.h"
#include "MarkdownRenderer.h"

#include "Pico/Agent/AgentRuntime.h"
#include "Pico/Agent/AgentCredentialStore.h"
#include "Pico/Agent/FakeAgentProvider.h"
#include "Pico/Agent/OpenAICompatibleProvider.h"
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
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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

enum class EAgentTurnIntent
{
    General,
    Play,
    Package
};

bool ContainsAny(std::string_view Text,
    std::initializer_list<std::string_view> Terms)
{
    return std::any_of(Terms.begin(), Terms.end(),
        [Text](std::string_view Term) { return Text.find(Term) != std::string_view::npos; });
}

EAgentTurnIntent ClassifyTurnIntent(std::string_view Prompt)
{
    std::string Lower(Prompt);
    std::transform(Lower.begin(), Lower.end(), Lower.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    const bool bPackage = ContainsAny(Lower,
        {"打包", "构建", "导出", "发布", "package", "packaging", "build", "export"});
    if (bPackage) return EAgentTurnIntent::Package;
    const bool bPlay = ContainsAny(Lower,
        {"运行", "启动", "试玩", "预览", "play", "run", "launch", "preview"});
    return bPlay ? EAgentTurnIntent::Play : EAgentTurnIntent::General;
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

struct FChatLine
{
    std::string Label;
    std::string Text;
    ImVec4 Color {0.82f, 0.84f, 0.88f, 1.0f};
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

std::string ToolCallMarkdown(const FAgentEvent& Event)
{
    return "`" + Event.ToolName + "`\n\n" + JsonCodeBlock(Event.PayloadJson);
}

std::string ToolResultMarkdown(const FAgentEvent& Event)
{
    std::string Text = Event.bSucceeded
        ? JsonCodeBlock(Event.PayloadJson)
        : Event.Content;
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
        FGameThreadDispatcher* InDispatcher)
        : EditorTools(InEditorTools), Dispatcher(InDispatcher)
    {
    }

    bool RequiresApproval(const FAgentToolCall& Call) const override
    {
        if (!IntentError(Call).empty()) return false;
        return EditorTools && EditorTools->RequiresApproval(Call);
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

    FAgentToolResult Execute(
        const FAgentToolCall& Call,
        const FCancellationToken* CancellationToken) override
    {
        const std::string BlockedReason = IntentError(Call);
        if (!BlockedReason.empty())
            return {Call.Id, false, "{}", BlockedReason, false};
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
            return {Call.Id, false, "{}", "Game Thread dispatcher is unavailable", false};
        }

        std::unique_lock Lock(Shared->Mutex);
        while (!Shared->bDone)
        {
            if (CancellationToken && CancellationToken->IsCancellationRequested())
                return {Call.Id, false, "{}", "Cancelled", false};
            Shared->Condition.wait_for(Lock, std::chrono::milliseconds(10));
        }
        LastTraceJson = Tools->GetLastExecutionTraceJson();
        return Shared->Result;
    }

    std::string GetLastExecutionTraceJson() const override
    {
        return LastTraceJson;
    }

private:
    std::string IntentError(const FAgentToolCall& Call) const
    {
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
    std::string LastTraceJson = "[]";
    std::atomic<EAgentTurnIntent> TurnIntent {EAgentTurnIntent::General};
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
        std::function<std::pair<bool, std::string>()> StartPlay,
        std::function<std::pair<bool, std::string>()> StopPlay)
        : EngineLoop(InEngineLoop)
        , TaskSystem(InTaskSystem)
        , Dispatcher(InDispatcher)
        , EditorTools(InEngineLoop, Selection, Transactions, &Approval,
            std::move(OnWorldChanged),
            {Commands, WorldDocument, std::move(StartPackage),
                std::move(StartPlay), std::move(StopPlay)})
        , GameThreadTools(&EditorTools, InDispatcher)
    {
        std::snprintf(Model.data(), Model.size(), "%s", "offline-fake");
        RefreshSessionList(true);
        RefreshCredentialState();
        RefreshSessionView();
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
        bStoredCredential = FAgentCredentialStore::TryLoadApiKey(
            ProviderId, ApiKey, &CredentialError);
        ClearSecret(ApiKey);
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
        for (const FAgentEvent& Event : Session->GetEvents())
        {
            if (Event.Type == EAgentEventType::Message)
            {
                const bool bUser = Event.Role == EAgentRole::User;
                NewLines.push_back({bUser ? "You" : "Assistant", Event.Content,
                    bUser ? ImVec4(0.45f, 0.78f, 1.0f, 1.0f)
                          : ImVec4(0.72f, 0.90f, 0.74f, 1.0f)});
            }
            else if (Event.Type == EAgentEventType::ToolCall)
            {
                NewLines.push_back({"Tool Call", ToolCallMarkdown(Event),
                    ImVec4(0.92f, 0.78f, 0.38f, 1.0f)});
            }
            else if (Event.Type == EAgentEventType::ToolResult)
            {
                NewLines.push_back({Event.bSucceeded ? "Tool Result" : "Tool Error",
                    ToolResultMarkdown(Event),
                    Event.bSucceeded ? ImVec4(0.68f, 0.84f, 0.72f, 1.0f)
                                     : ImVec4(1.0f, 0.42f, 0.36f, 1.0f)});
            }
            else if (Event.Type == EAgentEventType::Error)
            {
                NewLines.push_back({"Error", Event.Content,
                    ImVec4(1.0f, 0.42f, 0.36f, 1.0f)});
            }
        }
        std::lock_guard Lock(ViewMutex);
        Lines = std::move(NewLines);
        Status = std::string(ToString(Session->GetStatus()));
        bScrollToBottom.store(true);
    }

    std::unique_ptr<IAgentProvider> CreateProvider(
        EChatProvider ProviderType,
        std::string ModelName,
        std::string& OutError)
    {
        if (ProviderType == EChatProvider::Fake)
            return std::make_unique<FFakeSceneAgentProvider>(MakeRunNonce());

        FOpenAICompatibleProviderSettings Settings;
        Settings.Model = std::move(ModelName);
        Settings.ToolCatalogJson = EditorTools.BuildToolCatalogJson();
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
            "A project created from the third-person template is complete but must be opened in PicoEditor before tools can edit its active World; state this boundary plainly. "
            "Before editing reflected properties, call editor.object.describe and use the exact component object path, property name, current compound value, units, semantic, and range it returns. "
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
            FAgentCredentialStore::TryLoadApiKey(
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

    void Send()
    {
        if (!TaskSystem || bRunning.load() || Input[0] == '\0') return;
        const std::string Prompt = Input.data();
        Input.fill('\0');
        GameThreadTools.SetTurnIntent(ClassifyTurnIntent(Prompt));
        const EChatProvider SelectedProvider = Provider;
        const std::string SelectedModel = Model.data();
        const std::string SelectedProviderName =
            ProviderDisplayName(SelectedProvider);
        const std::string SelectedSessionId = SessionId;
        const std::filesystem::path SelectedSessionPath = SessionPath;
        std::string ProviderError;
        std::unique_ptr<IAgentProvider> NewProvider = CreateProvider(
            SelectedProvider, SelectedModel, ProviderError);
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
        }
        bRunning.store(true);
        std::shared_ptr<IAgentProvider> SharedProvider(std::move(NewProvider));
        ActiveTask = TaskSystem->Submit(
            "Pico Agent chat turn",
            [this, Prompt, AgentProvider = std::move(SharedProvider),
                SelectedProviderName, SelectedModel, SelectedSessionId,
                SelectedSessionPath](
                const FCancellationToken& Token) mutable
            {
                std::string Error;
                auto Session = FAgentSession::OpenOrCreate(
                    SelectedSessionId, SelectedSessionPath, &Error);
                FAgentRunResult Result;
                if (Session)
                {
                    FAgentBudget Budget;
                    Budget.MaxSteps = 12;
                    Budget.MaxToolCalls = 16;
                    Budget.MaxRepairAttempts = 2;
                    Budget.MaxElapsedMilliseconds = 120000;
                    FAgentRuntime Runtime(
                        *Session, *AgentProvider, GameThreadTools, Budget);
                    Result = Runtime.Run(Prompt, &Token);
                }
                else
                {
                    Result.Status = EAgentStatus::Failed;
                    Result.Error = Error;
                }
                RefreshSessionView();
                {
                    std::lock_guard Lock(ViewMutex);
                    Status = SelectedProviderName + " / " + SelectedModel
                        + ": " + std::string(ToString(Result.Status));
                    if (!Result.Error.empty()) Status += ": " + Result.Error;
                }
                bRunning.store(false);
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
            const char* DefaultModel = Provider == EChatProvider::DeepSeek
                ? "deepseek-v4-flash" : (Provider == EChatProvider::Kimi ? "kimi-k2.6" : "offline-fake");
            std::snprintf(Model.data(), Model.size(), "%s", DefaultModel);
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
            ImGui::EndDisabled();

            if (ImGui::CollapsingHeader(
                    "API Key (Project Local)", ImGuiTreeNodeFlags_DefaultOpen))
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
                    const bool bSaved = FAgentCredentialStore::SaveApiKey(
                        CredentialProviderId(Provider), ApiKey, &Error);
                    ClearSecret(ApiKey);
                    ApiKeyInput.fill('\0');
                    RefreshCredentialState();
                    std::lock_guard Lock(ViewMutex);
                    Status = bSaved
                        ? std::string(CredentialProviderId(Provider))
                            + " API key saved to the project Saved directory"
                        : Error;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(bRunning.load() || !bStoredCredential);
                if (ImGui::Button("Remove Saved Key"))
                {
                    std::string Error;
                    const bool bRemoved = FAgentCredentialStore::DeleteApiKey(
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
                    FAgentCredentialStore::GetStoragePath().string();
                ImGui::TextDisabled(
                    "Plaintext development secret; ignored by Git and excluded from packages.");
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
        if (ImGui::CollapsingHeader("Request Settings"))
        {
            ImGui::SliderInt("Timeout (seconds)", &TimeoutSeconds, 5, 120);
            ImGui::SliderInt("Retries", &MaxRetries, 0, 3);
            ImGui::TextDisabled(
                "Environment variables override project-local Saved/Agent/ApiKeys.ini values.");
        }

        std::string CurrentStatus;
        std::vector<FChatLine> CurrentLines;
        {
            std::lock_guard Lock(ViewMutex);
            CurrentStatus = Status;
            CurrentLines = Lines;
        }
        ImGui::Separator();
        ImGui::Text("Status: %s", CurrentStatus.c_str());

        FAgentToolCall PendingCall;
        EAgentToolPermission PendingPermission;
        std::string PendingDescription;
        if (Approval.GetPending(PendingCall, PendingPermission, PendingDescription))
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.17f, 0.08f, 1.0f));
            ImGui::BeginChild("AgentApproval", ImVec2(0.0f, 150.0f), true);
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), "Approval Required");
            ImGui::Text("%s  [%s]", PendingCall.Name.c_str(),
                ToString(PendingPermission).data());
            ImGui::TextWrapped("%s", PendingDescription.c_str());
            ImGui::TextWrapped("Arguments: %s", PendingCall.ArgumentsJson.c_str());
            if (ImGui::Button("Approve")) Approval.Decide(true);
            ImGui::SameLine();
            if (ImGui::Button("Reject")) Approval.Decide(false);
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
                std::string WrappedText = WrapSelectableText(Line.Text, TextWidth);
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
    std::string Status = "Idle";
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
    std::function<std::pair<bool, std::string>()> StartPlay,
    std::function<std::pair<bool, std::string>()> StopPlay)
    : Impl(std::make_unique<FImpl>(EngineLoop, Selection, Transactions,
        TaskSystem, Dispatcher, std::move(OnWorldChanged), Commands,
        WorldDocument, std::move(StartPackage), std::move(StartPlay),
        std::move(StopPlay)))
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
