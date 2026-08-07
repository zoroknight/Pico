#include "PicoEditorApp.h"

#include "Pico/Core/Paths.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
#ifndef PICO_DEFAULT_PROJECT_FILE
#define PICO_DEFAULT_PROJECT_FILE ""
#endif

std::filesystem::path FindProjectFile(int Argc, char** Argv)
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
        if (!Argument.starts_with("-") && PositionalPath.extension() == ".pico")
        {
            return PositionalPath;
        }
    }

    return std::filesystem::path(PICO_DEFAULT_PROJECT_FILE);
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
}

int main(int Argc, char** Argv)
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "GLFW initialization failed\n");
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
    int ExitCode = 1;

    try
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        bImGuiContextCreated = true;
        ImGuiIO& IO = ImGui::GetIO();
        IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        IO.IniFilename = nullptr;
        const float UiScale = FindUiScale(Argc, Argv);
        const std::filesystem::path InterfaceFont = "C:/Windows/Fonts/segoeui.ttf";
        if (std::filesystem::is_regular_file(InterfaceFont))
        {
            IO.FontDefault = IO.Fonts->AddFontFromFileTTF(
                InterfaceFont.string().c_str(),
                16.0f * UiScale);
        }
        if (IO.FontDefault == nullptr)
        {
            ImFontConfig FontConfig;
            FontConfig.SizePixels = 16.0f * UiScale;
            IO.FontDefault = IO.Fonts->AddFontDefault(&FontConfig);
        }
        ApplyEditorStyle(UiScale);

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

        bRendererInitialized = ViewportRenderer.Initialize(&LoadOpenGLProcedure);
        if (!bRendererInitialized)
        {
            throw std::runtime_error("Scene viewport renderer initialization failed");
        }

        const std::filesystem::path ProjectFile = FindProjectFile(Argc, Argv);
        ExitCode = EngineLoop.PreInit(Argc, Argv, ProjectFile);
        if (ExitCode == 0)
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
                }
            }
        }
        if (ExitCode == 0)
        {
            ExitCode = EngineLoop.Init();
        }

        if (ExitCode == 0)
        {
            Pico::FPicoEditorApp App(&EngineLoop, &ViewportRenderer, Window);
            while (!EngineLoop.ShouldExit())
            {
                glfwPollEvents();
                if (glfwWindowShouldClose(Window))
                {
                    glfwSetWindowShouldClose(Window, GLFW_FALSE);
                    App.RequestClose();
                }
                EngineLoop.Tick();

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                App.Draw();

                ImGui::Render();
                int FramebufferWidth = 0;
                int FramebufferHeight = 0;
                glfwGetFramebufferSize(Window, &FramebufferWidth, &FramebufferHeight);
                glViewport(0, 0, FramebufferWidth, FramebufferHeight);
                glClearColor(0.045f, 0.05f, 0.055f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(Window);
                if (App.ShouldClose())
                {
                    break;
                }
            }
        }
    }
    catch (const std::exception& Exception)
    {
        std::fprintf(stderr, "Pico Editor fatal error: %s\n", Exception.what());
        ExitCode = 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "Pico Editor fatal error: unknown exception\n");
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
    return ExitCode;
}
