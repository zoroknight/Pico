#include "InspectorApp.h"

#include "Pico/Object/ObjectSystem.h"
#include "Pico/Samples/DemoCharacter.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{
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

void ApplyPicoStyle(float Scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& Style = ImGui::GetStyle();
    Style.WindowRounding = 4.0f;
    Style.FrameRounding = 3.0f;
    Style.ChildRounding = 3.0f;
    Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.110f, 0.115f, 1.0f);
    Style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.42f, 0.38f, 1.0f);
    Style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.13f, 0.53f, 0.47f, 1.0f);
    Style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.62f, 0.54f, 1.0f);
    Style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.23f, 0.24f, 1.0f);
    Style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.48f, 0.42f, 1.0f);
    Style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.60f, 0.51f, 1.0f);
    Style.Colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.67f, 0.24f, 1.0f);
    Style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.18f, 1.0f);
    Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.25f, 1.0f);
    Style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.38f, 0.35f, 1.0f);
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
    const GLFWvidmode* VideoMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    const int WindowWidth = VideoMode != nullptr ? std::min(1180, VideoMode->width - 80) : 1024;
    const int WindowHeight = VideoMode != nullptr ? std::min(720, VideoMode->height - 100) : 720;
    GLFWwindow* Window = glfwCreateWindow(WindowWidth, WindowHeight, "Pico Inspector", nullptr, nullptr);
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
    ApplyPicoStyle(UiScale);

    ImGui_ImplGlfw_InitForOpenGL(Window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    int ExitCode = 0;
    if (!Pico::PObjectSystem::Init()
        || !Pico::PDemoCharacter::RegisterClass()
        || !Pico::PDemoHealthObserver::RegisterClass())
    {
        ExitCode = 1;
    }
    else
    {
        {
            Pico::FInspectorApp App;
            while (!glfwWindowShouldClose(Window))
            {
                glfwPollEvents();
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                App.Draw();

                ImGui::Render();
                int FramebufferWidth = 0;
                int FramebufferHeight = 0;
                glfwGetFramebufferSize(Window, &FramebufferWidth, &FramebufferHeight);
                glViewport(0, 0, FramebufferWidth, FramebufferHeight);
                glClearColor(0.055f, 0.060f, 0.065f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(Window);
            }
        }
        Pico::PObjectSystem::Shutdown();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return ExitCode;
}
