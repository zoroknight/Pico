#include "Pico/Launch/GameLaunch.h"

#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/World.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
struct FWindowInputContext
{
    Pico::FInputSystem* InputSystem = nullptr;
};

std::filesystem::path FindProjectFile(
    int Argc,
    char** Argv,
    const std::filesystem::path& DefaultProjectFile)
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
        const std::filesystem::path PositionalPath { std::string(Argument) };
        if (!Argument.starts_with("-") && PositionalPath.extension() == ".pico")
        {
            return PositionalPath;
        }
    }
    return DefaultProjectFile;
}

Pico::FOpenGLProcedure LoadOpenGLProcedure(const char* Name)
{
    return reinterpret_cast<Pico::FOpenGLProcedure>(glfwGetProcAddress(Name));
}

Pico::EKey TranslateKey(int Key)
{
    if (Key >= GLFW_KEY_A && Key <= GLFW_KEY_Z)
    {
        return static_cast<Pico::EKey>(
            static_cast<int>(Pico::EKey::A) + (Key - GLFW_KEY_A));
    }
    switch (Key)
    {
    case GLFW_KEY_SPACE: return Pico::EKey::Space;
    case GLFW_KEY_ESCAPE: return Pico::EKey::Escape;
    case GLFW_KEY_ENTER: return Pico::EKey::Enter;
    case GLFW_KEY_TAB: return Pico::EKey::Tab;
    case GLFW_KEY_LEFT_SHIFT: return Pico::EKey::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT: return Pico::EKey::RightShift;
    case GLFW_KEY_LEFT_CONTROL: return Pico::EKey::LeftControl;
    case GLFW_KEY_RIGHT_CONTROL: return Pico::EKey::RightControl;
    case GLFW_KEY_UP: return Pico::EKey::Up;
    case GLFW_KEY_DOWN: return Pico::EKey::Down;
    case GLFW_KEY_LEFT: return Pico::EKey::Left;
    case GLFW_KEY_RIGHT: return Pico::EKey::Right;
    default: return Pico::EKey::Unknown;
    }
}

Pico::EKey TranslateMouseButton(int Button)
{
    switch (Button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return Pico::EKey::MouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT: return Pico::EKey::MouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE: return Pico::EKey::MouseMiddle;
    default: return Pico::EKey::Unknown;
    }
}

FWindowInputContext* GetInputContext(GLFWwindow* Window)
{
    return static_cast<FWindowInputContext*>(glfwGetWindowUserPointer(Window));
}

void OnKey(GLFWwindow* Window, int Key, int, int Action, int)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context == nullptr || Context->InputSystem == nullptr || Action == GLFW_REPEAT)
    {
        return;
    }
    Context->InputSystem->SetKeyState(TranslateKey(Key), Action == GLFW_PRESS);
    if (Key == GLFW_KEY_ESCAPE && Action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(Window, GLFW_TRUE);
    }
}

void OnMouseButton(GLFWwindow* Window, int Button, int Action, int)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->SetKeyState(
            TranslateMouseButton(Button), Action == GLFW_PRESS);
    }
}

void OnCursorPosition(GLFWwindow* Window, double X, double Y)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->SetMousePosition(X, Y);
    }
}

void OnScroll(GLFWwindow* Window, double, double YOffset)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->AddMouseWheelDelta(YOffset);
    }
}

void OnFocus(GLFWwindow* Window, int Focused)
{
    FWindowInputContext* Context = GetInputContext(Window);
    if (Context != nullptr && Context->InputSystem != nullptr)
    {
        Context->InputSystem->SetFocused(Focused == GLFW_TRUE);
    }
}
}

namespace Pico
{
int RunPicoGame(
    int Argc,
    char** Argv,
    IGameModule* GameModule,
    const std::filesystem::path& DefaultProjectFile)
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* Window = glfwCreateWindow(1280, 720, "Pico Game", nullptr, nullptr);
    if (Window == nullptr)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);

    FGameEngine GameEngine(GameModule);
    FSceneViewportRenderer Renderer;
    bool bRendererInitialized = false;
    int ExitCode = 1;

    try
    {
        bRendererInitialized = Renderer.Initialize(&LoadOpenGLProcedure);
        if (!bRendererInitialized)
        {
            throw std::runtime_error("scene renderer initialization failed");
        }

        const std::filesystem::path ProjectFile =
            FindProjectFile(Argc, Argv, DefaultProjectFile);
        ExitCode = GameEngine.PreInit(Argc, Argv, ProjectFile);
        if (ExitCode == 0)
        {
            ExitCode = GameEngine.Init();
        }

        FWindowInputContext InputContext { &GameEngine.GetInputSystem() };
        glfwSetWindowUserPointer(Window, &InputContext);
        glfwSetKeyCallback(Window, &OnKey);
        glfwSetMouseButtonCallback(Window, &OnMouseButton);
        glfwSetCursorPosCallback(Window, &OnCursorPosition);
        glfwSetScrollCallback(Window, &OnScroll);
        glfwSetWindowFocusCallback(Window, &OnFocus);

        while (ExitCode == 0
            && !glfwWindowShouldClose(Window)
            && !GameEngine.ShouldExit())
        {
            FInputSystem& Input = GameEngine.GetInputSystem();
            Input.BeginFrame();
            glfwPollEvents();
            GameEngine.Tick();

            int Width = 0;
            int Height = 0;
            glfwGetFramebufferSize(Window, &Width, &Height);
            if (Width > 0 && Height > 0)
            {
                Renderer.Resize(
                    static_cast<uint32>(Width),
                    static_cast<uint32>(Height));
                FSceneView View;
                TryBuildActiveCameraView(
                    GameEngine.GetEngineLoop().GetWorld(), View, true);
                Renderer.Render(
                    GameEngine.GetEngineLoop().GetWorld(),
                    GameEngine.GetEngineLoop().GetAssetRegistry(),
                    GameEngine.GetEngineLoop().GetAssetManager(),
                    View,
                    {},
                    false);
                Renderer.PresentToBackBuffer(
                    static_cast<uint32>(Width),
                    static_cast<uint32>(Height));
                glfwSwapBuffers(Window);
            }
            Input.EndFrame();
        }
    }
    catch (const std::exception& Exception)
    {
        std::fprintf(stderr, "Pico Game fatal error: %s\n", Exception.what());
        ExitCode = 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "Pico Game fatal error: unknown exception\n");
        ExitCode = 1;
    }

    glfwSetWindowUserPointer(Window, nullptr);
    GameEngine.Exit();
    if (bRendererInitialized)
    {
        Renderer.Shutdown();
    }
    glfwDestroyWindow(Window);
    glfwTerminate();
    return ExitCode;
}
}
