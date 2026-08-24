#include "PicoEditorApp.h"
#include "NativeFileDialog.h"

#include "Pico/Editor/EditorProjectManager.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Paths.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/ActorBlueprint.h"
#include "Pico/Render/SceneViewportRenderer.h"

#if defined(PICO_EDITOR_WITH_SANDBOX)
#include "PicoSandbox/SandboxModule.h"
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
bool HasArgument(int Argc, char** Argv, std::string_view Expected)
{
    for (int Index = 1; Index < Argc; ++Index)
    {
        if (std::string_view(Argv[Index]) == Expected)
        {
            return true;
        }
    }
    return false;
}

void ConfigureEditorConsole(int Argc, char** Argv)
{
#if defined(_WIN32)
    const bool bShowConsole = HasArgument(Argc, Argv, "-console")
        || HasArgument(Argc, Argv, "-log");
    Pico::FLog::SetConsoleOutputEnabled(bShowConsole);
    if (!bShowConsole || !AllocConsole())
    {
        return;
    }
    SetConsoleOutputCP(CP_UTF8);
    FILE* ConsoleStream = nullptr;
    freopen_s(&ConsoleStream, "CONOUT$", "w", stdout);
    freopen_s(&ConsoleStream, "CONOUT$", "w", stderr);
#else
    (void)Argc;
    (void)Argv;
#endif
}

void ReportFatalError(std::string_view Message)
{
    Pico::FLog::Write("LogEditor", Pico::ELogLevel::Error, Message);
#if defined(_WIN32)
    const std::string Text(Message);
    MessageBoxA(nullptr, Text.c_str(), "Pico Editor", MB_OK | MB_ICONERROR);
#endif
}

std::filesystem::path FindProjectSelection(int Argc, char** Argv)
{
    constexpr std::string_view ProjectPrefix = "-project=";
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string_view Argument = Argv[Index];
        if (Argument.starts_with(ProjectPrefix))
        {
            return std::filesystem::path(
                std::string(Argument.substr(ProjectPrefix.size())));
        }

        const std::filesystem::path PositionalPath =
            std::filesystem::path(std::string(Argument));
        if (!Argument.starts_with("-"))
        {
            return PositionalPath;
        }
    }
    return {};
}

float FindUiScale(int Argc, char** Argv)
{
    constexpr std::string_view ScalePrefix = "-uiscale=";
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string_view Argument = Argv[Index];
        if (!Argument.starts_with(ScalePrefix))
        {
            continue;
        }

        const std::string Value(Argument.substr(ScalePrefix.size()));
        char* End = nullptr;
        const float Scale = std::strtof(Value.c_str(), &End);
        if (End != Value.c_str() && End != nullptr && *End == '\0')
        {
            return std::clamp(Scale, 0.75f, 2.5f);
        }
    }
    return 1.4f;
}

Pico::FOpenGLProcedure LoadOpenGLProcedure(const char* Name)
{
    return reinterpret_cast<Pico::FOpenGLProcedure>(glfwGetProcAddress(Name));
}

void ApplyEditorStyle(float Scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& Style = ImGui::GetStyle();
    Style.WindowRounding = 3.0f;
    Style.FrameRounding = 2.0f;
    Style.ChildRounding = 2.0f;
    Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.095f, 0.10f, 1.0f);
    Style.Colors[ImGuiCol_Header] = ImVec4(0.13f, 0.34f, 0.31f, 1.0f);
    Style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.45f, 0.40f, 1.0f);
    Style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.19f, 0.55f, 0.47f, 1.0f);
    Style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.21f, 0.22f, 1.0f);
    Style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.42f, 0.37f, 1.0f);
    Style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.53f, 0.45f, 1.0f);
    Style.Colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.67f, 0.25f, 1.0f);
    Style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.0f);
    Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.23f, 0.24f, 1.0f);
    Style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.13f, 0.34f, 0.31f, 1.0f);
    Style.ScaleAllSizes(Scale);
}

void PresentImGuiFrame(GLFWwindow* Window, const ImGuiIO& IO)
{
    ImGui::Render();
    int FramebufferWidth = 0;
    int FramebufferHeight = 0;
    glfwGetFramebufferSize(Window, &FramebufferWidth, &FramebufferHeight);
    glViewport(0, 0, FramebufferWidth, FramebufferHeight);
    glClearColor(0.045f, 0.05f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(Window);
    if ((IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
        GLFWwindow* BackupContext = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(BackupContext);
    }
}

std::optional<std::filesystem::path> RunProjectBrowser(
    GLFWwindow* Window,
    Pico::FEditorProjectHistory& History,
    const std::filesystem::path& SettingsFile,
    std::string InitialMessage)
{
    std::optional<std::filesystem::path> SelectedProject;
    int SelectedRecent = History.GetRecentProjects().empty() ? -1 : 0;
    std::string Message = std::move(InitialMessage);
    glfwSetWindowTitle(Window, "Pico Project Browser");
    while (!SelectedProject.has_value() && !glfwWindowShouldClose(Window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            Viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_Always);
        ImGui::Begin(
            "Pico Project Browser",
            nullptr,
            ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoDocking);
        ImGui::TextUnformatted("Pico");
        ImGui::TextDisabled("Open a project descriptor or a folder containing one .pico file");
        ImGui::Separator();
        ImGui::TextUnformatted("Recent Projects");

        const auto& RecentProjects = History.GetRecentProjects();
        if (RecentProjects.empty())
        {
            ImGui::TextDisabled("No recent projects");
            ImGui::Dummy(ImVec2(0.0f, 250.0f));
        }
        else if (ImGui::BeginTable(
                     "RecentProjects", 2,
                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                         | ImGuiTableFlags_ScrollY,
                     ImVec2(0.0f, 285.0f)))
        {
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (std::size_t Index = 0; Index < RecentProjects.size(); ++Index)
            {
                const std::filesystem::path& Project = RecentProjects[Index];
                ImGui::PushID(static_cast<int>(Index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool bSelected = SelectedRecent == static_cast<int>(Index);
                if (ImGui::Selectable(
                        Project.stem().string().c_str(), bSelected,
                        ImGuiSelectableFlags_SpanAllColumns
                            | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    SelectedRecent = static_cast<int>(Index);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        SelectedProject = Project;
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Project.string().c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!Message.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.38f, 0.30f, 1.0f));
            ImGui::TextWrapped("%s", Message.c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextDisabled("One editor process hosts one project.");
        }

        const auto ResolveSelection = [&](const std::filesystem::path& Selection)
        {
            const Pico::FEditorProjectResolution Resolution =
                Pico::ResolveEditorProjectPath(Selection);
            if (Resolution.IsResolved())
            {
                SelectedProject = Resolution.ProjectFile;
                Message.clear();
            }
            else
            {
                Message = Resolution.Message;
            }
        };

        ImGui::BeginDisabled(
            SelectedRecent < 0
            || SelectedRecent >= static_cast<int>(RecentProjects.size()));
        if (ImGui::Button("Open Selected"))
        {
            SelectedProject = RecentProjects[static_cast<std::size_t>(SelectedRecent)];
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
        {
            const std::filesystem::path Removed =
                RecentProjects[static_cast<std::size_t>(SelectedRecent)];
            History.Remove(Removed);
            History.Save(SettingsFile);
            SelectedRecent = History.GetRecentProjects().empty() ? -1 : 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Open Project File..."))
        {
            if (const auto File = Pico::OpenProjectFileDialog()) ResolveSelection(*File);
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse Folder..."))
        {
            if (const auto Folder = Pico::OpenProjectFolderDialog()) ResolveSelection(*Folder);
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit"))
        {
            glfwSetWindowShouldClose(Window, GLFW_TRUE);
        }
        ImGui::End();

        PresentImGuiFrame(Window, ImGui::GetIO());
    }
    return SelectedProject;
}
}

int main(int Argc, char** Argv)
{
    ConfigureEditorConsole(Argc, Argv);
    if (!glfwInit())
    {
        ReportFatalError("GLFW initialization failed");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    const GLFWvidmode* VideoMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    const int WindowWidth = VideoMode != nullptr ? std::min(1600, VideoMode->width - 40) : 1400;
    const int WindowHeight = VideoMode != nullptr ? std::min(900, VideoMode->height - 60) : 820;
    GLFWwindow* Window = glfwCreateWindow(WindowWidth, WindowHeight, "Pico Editor", nullptr, nullptr);
    if (Window == nullptr)
    {
        ReportFatalError("Pico Editor window creation failed");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);

    Pico::FSceneViewportRenderer ViewportRenderer;
    Pico::FEngineLoop EngineLoop;
    bool bImGuiContextCreated = false;
    bool bImGuiGlfwInitialized = false;
    bool bImGuiOpenGLInitialized = false;
    bool bRendererInitialized = false;
    std::string LayoutIniPath;
    Pico::FEditorProjectHistory ProjectHistory;
    const std::filesystem::path EditorSettingsFile =
        Pico::GetEditorUserSettingsFile();
    ProjectHistory.Load(EditorSettingsFile);
    bool bProjectChosen = false;
    int ExitCode = 1;

    try
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        bImGuiContextCreated = true;
        ImGuiIO& IO = ImGui::GetIO();
        IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard
            | ImGuiConfigFlags_DockingEnable
            | ImGuiConfigFlags_ViewportsEnable;
        IO.IniFilename = nullptr;
        const float UiScale = FindUiScale(Argc, Argv);
        const float FontSize = 16.0f * UiScale;
        const std::filesystem::path InterfaceFont = "C:/Windows/Fonts/segoeui.ttf";
        if (std::filesystem::is_regular_file(InterfaceFont))
        {
            IO.FontDefault = IO.Fonts->AddFontFromFileTTF(
                InterfaceFont.string().c_str(),
                FontSize);
        }
        if (IO.FontDefault == nullptr)
        {
            ImFontConfig FontConfig;
            FontConfig.SizePixels = FontSize;
            IO.FontDefault = IO.Fonts->AddFontDefault(&FontConfig);
        }
        for (const std::filesystem::path& ChineseFont : {
                 std::filesystem::path("C:/Windows/Fonts/msyh.ttc"),
                 std::filesystem::path("C:/Windows/Fonts/simhei.ttf") })
        {
            if (!std::filesystem::is_regular_file(ChineseFont))
                continue;
            ImFontConfig MergeConfig;
            MergeConfig.MergeMode = true;
            MergeConfig.PixelSnapH = true;
            if (IO.Fonts->AddFontFromFileTTF(
                    ChineseFont.string().c_str(), FontSize, &MergeConfig,
                    IO.Fonts->GetGlyphRangesChineseFull()) != nullptr)
            {
                break;
            }
        }
        const std::filesystem::path SymbolFont =
            "C:/Windows/Fonts/seguisym.ttf";
        if (std::filesystem::is_regular_file(SymbolFont))
        {
            static const ImWchar SymbolRanges[] = {
                0x2000, 0x206F, // General punctuation.
                0x2190, 0x21FF, // Arrows.
                0x2300, 0x23FF, // Technical symbols.
                0x2500, 0x257F, // Box drawing.
                0x2600, 0x26FF, // Miscellaneous symbols.
                0x2700, 0x27BF, // Dingbats, including check marks.
                0x2B00, 0x2BFF, // Miscellaneous arrows and symbols.
                0
            };
            ImFontConfig MergeConfig;
            MergeConfig.MergeMode = true;
            MergeConfig.PixelSnapH = true;
            IO.Fonts->AddFontFromFileTTF(
                SymbolFont.string().c_str(), FontSize, &MergeConfig,
                SymbolRanges);
        }
        ApplyEditorStyle(UiScale);
        ImGui::GetStyle().WindowRounding = 0.0f;
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;

        bImGuiGlfwInitialized = ImGui_ImplGlfw_InitForOpenGL(Window, true);
        if (!bImGuiGlfwInitialized)
        {
            throw std::runtime_error("ImGui GLFW backend initialization failed");
        }
        bImGuiOpenGLInitialized = ImGui_ImplOpenGL3_Init("#version 130");
        if (!bImGuiOpenGLInitialized)
        {
            throw std::runtime_error("ImGui OpenGL backend initialization failed");
        }

        std::filesystem::path ProjectFile;
        std::string ProjectMessage;
        const std::filesystem::path RequestedProject =
            FindProjectSelection(Argc, Argv);
        if (!RequestedProject.empty())
        {
            const Pico::FEditorProjectResolution Resolution =
                Pico::ResolveEditorProjectPath(RequestedProject);
            ProjectFile = Resolution.ProjectFile;
            ProjectMessage = Resolution.Message;
        }
        if (ProjectFile.empty())
        {
            const std::optional<std::filesystem::path> BrowserSelection =
                RunProjectBrowser(
                    Window, ProjectHistory, EditorSettingsFile,
                    std::move(ProjectMessage));
            if (BrowserSelection.has_value())
            {
                ProjectFile = *BrowserSelection;
            }
        }
        bProjectChosen = !ProjectFile.empty();
        if (!bProjectChosen)
        {
            ExitCode = 0;
        }
        if (bProjectChosen)
        {
            bRendererInitialized = ViewportRenderer.Initialize(&LoadOpenGLProcedure);
            if (!bRendererInitialized)
            {
                throw std::runtime_error("Scene viewport renderer initialization failed");
            }
            ExitCode = EngineLoop.PreInit(Argc, Argv, ProjectFile);
            if (ExitCode == 0)
            {
                std::filesystem::path LogFile;
                if (!Pico::FPaths::TryGetProjectWritePath(
                        Pico::EProjectWriteRoot::Saved,
                        "Logs/PicoEditor.log",
                        LogFile)
                    || !Pico::FLog::SetOutputFile(LogFile))
                {
                    Pico::FLog::Write(
                        "LogEditor",
                        Pico::ELogLevel::Warning,
                        "Could not open the project editor log file");
                }
            }
        }
        if (bProjectChosen && ExitCode == 0)
        {
            std::filesystem::path LayoutPath;
            if (Pico::FPaths::TryGetProjectWritePath(
                    Pico::EProjectWriteRoot::Saved,
                    "Editor/PicoEditorLayout.ini",
                    LayoutPath))
            {
                std::error_code Error;
                std::filesystem::create_directories(LayoutPath.parent_path(), Error);
                if (!Error)
                {
                    LayoutIniPath = LayoutPath.string();
                    IO.IniFilename = LayoutIniPath.c_str();
                    if (std::filesystem::is_regular_file(LayoutPath))
                    {
                        ImGui::LoadIniSettingsFromDisk(LayoutIniPath.c_str());
                    }
                }
            }
        }
        if (bProjectChosen && ExitCode == 0)
        {
            ExitCode = EngineLoop.Init();
        }
#if defined(PICO_EDITOR_WITH_SANDBOX)
        if (bProjectChosen && ExitCode == 0
            && !PicoSandbox::RegisterSandboxGameplayClasses())
        {
            throw std::runtime_error("project gameplay class registration failed");
        }
#endif
        if (bProjectChosen && ExitCode == 0
            && !Pico::CompileProjectActorBlueprints(EngineLoop.GetAssetRegistry()))
        {
            throw std::runtime_error("project Actor Blueprint compilation failed");
        }

        if (bProjectChosen && ExitCode == 0)
        {
            ProjectHistory.Add(ProjectFile);
            ProjectHistory.Save(EditorSettingsFile);
            Pico::FPicoEditorApp App(&EngineLoop, &ViewportRenderer, Window);
            Pico::FEngineFrameCallbacks FrameCallbacks;
            FrameCallbacks.BeforeWorldTick =
                [&App](float) { App.PumpGameThreadTasks(); };
            while (!EngineLoop.ShouldExit())
            {
                glfwPollEvents();
                if (glfwWindowShouldClose(Window))
                {
                    glfwSetWindowShouldClose(Window, GLFW_FALSE);
                    App.RequestClose();
                }
                EngineLoop.Tick(FrameCallbacks);

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                App.Draw();

                PresentImGuiFrame(Window, IO);
                if (App.ShouldClose())
                {
                    break;
                }
            }
        }
    }
    catch (const std::exception& Exception)
    {
        ReportFatalError(std::string("Pico Editor fatal error: ") + Exception.what());
        ExitCode = 1;
    }
    catch (...)
    {
        ReportFatalError("Pico Editor fatal error: unknown exception");
        ExitCode = 1;
    }

    EngineLoop.Exit();
    if (bRendererInitialized)
    {
        ViewportRenderer.Shutdown();
    }
    if (bImGuiContextCreated && !LayoutIniPath.empty())
    {
        ImGui::SaveIniSettingsToDisk(LayoutIniPath.c_str());
    }
    if (bImGuiOpenGLInitialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (bImGuiGlfwInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
    }
    if (bImGuiContextCreated)
    {
        ImGui::DestroyContext();
    }
    glfwDestroyWindow(Window);
    glfwTerminate();
    Pico::FLog::CloseOutputFile();
    return ExitCode;
}
