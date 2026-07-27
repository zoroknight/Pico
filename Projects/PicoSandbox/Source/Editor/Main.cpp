#include "SandboxEditorApp.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace
{
void ApplySandboxStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& Style = ImGui::GetStyle();
    Style.WindowRounding = 4.0f;
    Style.FrameRounding = 3.0f;
    Style.ChildRounding = 3.0f;
    Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.105f, 1.0f);
    Style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.36f, 0.33f, 1.0f);
    Style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.48f, 0.43f, 1.0f);
    Style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.58f, 0.50f, 1.0f);
    Style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.22f, 0.23f, 1.0f);
    Style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.46f, 0.40f, 1.0f);
    Style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.57f, 0.49f, 1.0f);
    Style.Colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.67f, 0.25f, 1.0f);
    Style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.0f);
    Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.24f, 1.0f);
    Style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.36f, 0.33f, 1.0f);
}
}

int main()
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
    const int WindowWidth = VideoMode != nullptr ? std::min(1100, VideoMode->width - 40) : 1100;
    const int WindowHeight = VideoMode != nullptr ? std::min(680, VideoMode->height - 40) : 680;
    GLFWwindow* Window = glfwCreateWindow(WindowWidth, WindowHeight, "Pico Sandbox", nullptr, nullptr);
    if (Window == nullptr)
    {
        glfwTerminate();
        return 1;
    }
    if (VideoMode != nullptr)
    {
        glfwSetWindowPos(Window, 20, 20);
    }

    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplySandboxStyle();

    ImGui_ImplGlfw_InitForOpenGL(Window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        PicoSandbox::FSandboxEditorApp App;
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
            glClearColor(0.045f, 0.05f, 0.055f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(Window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return 0;
}
