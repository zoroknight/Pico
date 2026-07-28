#include "PicoEditorApp.h"

#include "Pico/Engine/EngineLoop.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

void ApplyEditorStyle()
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
    const GLFWvidmode* VideoMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    const int WindowWidth = VideoMode != nullptr ? std::min(1280, VideoMode->width - 60) : 1200;
    const int WindowHeight = VideoMode != nullptr ? std::min(760, VideoMode->height - 80) : 720;
    GLFWwindow* Window = glfwCreateWindow(WindowWidth, WindowHeight, "Pico Editor", nullptr, nullptr);
    if (Window == nullptr)
    {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyEditorStyle();

    ImGui_ImplGlfw_InitForOpenGL(Window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    Pico::FEngineLoop EngineLoop;
    const std::filesystem::path ProjectFile = FindProjectFile(Argc, Argv);
    int ExitCode = EngineLoop.PreInit(Argc, Argv, ProjectFile);
    if (ExitCode == 0)
    {
        ExitCode = EngineLoop.Init();
    }

    if (ExitCode == 0)
    {
        Pico::FPicoEditorApp App(EngineLoop.GetWorld());
        while (!glfwWindowShouldClose(Window) && !EngineLoop.ShouldExit())
        {
            glfwPollEvents();
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
        }
    }

    EngineLoop.Exit();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return ExitCode;
}
