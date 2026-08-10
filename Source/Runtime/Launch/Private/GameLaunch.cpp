#include "Pico/Launch/GameLaunch.h"

#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/World.h"
#include "Pico/Render/SceneViewportRenderer.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct FWindowInputContext
{
    Pico::FInputSystem* InputSystem = nullptr;
};

struct FGameplayDebugPanel
{
    void Update(Pico::FGameEngine& GameEngine)
    {
        Pico::PGameInstance* GameInstance = GameEngine.GetGameInstance();
        Pico::PLocalPlayer* LocalPlayer = GameInstance != nullptr
            ? GameInstance->GetPrimaryLocalPlayer()
            : nullptr;
        Pico::PPlayerController* Controller = LocalPlayer != nullptr
            ? LocalPlayer->GetPlayerController()
            : nullptr;
        Pico::PWorld* World = GameEngine.GetEngineLoop().GetWorld();
        Observe("World", World, WorldHandle);
        Observe("GameMode", World != nullptr ? World->GetGameMode() : nullptr, GameModeHandle);
        Observe("GameState", World != nullptr ? World->GetGameState() : nullptr, GameStateHandle);
        Observe("LocalPlayer", LocalPlayer, LocalPlayerHandle);
        Observe("PlayerController", Controller, ControllerHandle);
        Observe("PlayerState", Controller != nullptr ? Controller->GetPlayerState() : nullptr, PlayerStateHandle);
        Pico::PPawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
        Observe("Pawn", Pawn, PawnHandle);
        if (Pawn != nullptr)
        {
            LastPawnHandle = Pawn->GetHandle();
        }
    }

    void Draw(Pico::FGameEngine& GameEngine)
    {
        if (!bVisible)
        {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(470.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Gameplay Debug", &bVisible))
        {
            ImGui::End();
            return;
        }

        Pico::PGameInstance* GameInstance = GameEngine.GetGameInstance();
        Pico::PLocalPlayer* LocalPlayer = GameInstance != nullptr
            ? GameInstance->GetPrimaryLocalPlayer()
            : nullptr;
        Pico::PPlayerController* Controller = LocalPlayer != nullptr
            ? LocalPlayer->GetPlayerController()
            : nullptr;
        Pico::PPlayerState* PlayerState = Controller != nullptr
            ? Controller->GetPlayerState()
            : nullptr;
        Pico::PPawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
        Pico::PWorld* World = GameEngine.GetEngineLoop().GetWorld();

        ImGui::TextUnformatted("Runtime object chain");
        DrawObject("GameInstance", GameInstance);
        DrawObject("LocalPlayer", LocalPlayer);
        DrawObject("World", World);
        DrawObject("GameMode", World != nullptr ? World->GetGameMode() : nullptr);
        DrawObject("GameState", World != nullptr ? World->GetGameState() : nullptr);
        DrawObject("PlayerController", Controller);
        DrawObject("PlayerState", PlayerState);
        DrawObject("Controlled Pawn", Pawn);
        if (PlayerState != nullptr)
        {
            ImGui::Text("PlayerId: %d   Score: %.1f   Spectator: %s",
                PlayerState->GetPlayerId(),
                PlayerState->GetScore(),
                PlayerState->IsSpectator() ? "yes" : "no");
        }

        ImGui::Separator();
        const bool bCanRestart = World != nullptr
            && World->GetGameMode() != nullptr
            && Controller != nullptr;
        if (!bCanRestart) ImGui::BeginDisabled();
        if (ImGui::Button("Restart Player"))
        {
            World->GetGameMode()->RestartPlayer(Controller);
        }
        ImGui::SameLine();
        if (ImGui::Button("Destroy Pawn") && Pawn != nullptr)
        {
            Pawn->Destroy();
        }
        if (!bCanRestart) ImGui::EndDisabled();

        const bool bCanUnPossess = Controller != nullptr && Pawn != nullptr;
        if (!bCanUnPossess) ImGui::BeginDisabled();
        if (ImGui::Button("UnPossess"))
        {
            Controller->UnPossess();
            if (PlayerState != nullptr) PlayerState->SetIsSpectator(true);
        }
        if (!bCanUnPossess) ImGui::EndDisabled();
        ImGui::SameLine();
        Pico::PObject* LastPawnObject = Pico::ResolveObject(LastPawnHandle);
        Pico::PPawn* AvailablePawn = LastPawnObject != nullptr
                && LastPawnObject->IsA(Pico::PPawn::StaticClass())
                && !static_cast<Pico::PPawn*>(LastPawnObject)->IsPendingDestroy()
            ? static_cast<Pico::PPawn*>(LastPawnObject)
            : nullptr;
        const bool bCanPossess = Controller != nullptr
            && Controller->GetPawn() == nullptr
            && AvailablePawn != nullptr;
        if (!bCanPossess) ImGui::BeginDisabled();
        if (ImGui::Button("Possess Last Pawn"))
        {
            Controller->Possess(AvailablePawn);
            if (PlayerState != nullptr) PlayerState->SetIsSpectator(false);
        }
        if (!bCanPossess) ImGui::EndDisabled();

        if (ImGui::Button("Reload Map"))
        {
            GameEngine.LoadMap(GameEngine.GetDefaultMapPath());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Events"))
        {
            Events.clear();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Lifecycle events");
        ImGui::BeginChild("GameplayEvents", ImVec2(0.0f, 0.0f), true);
        for (const std::string& Event : Events)
        {
            ImGui::TextUnformatted(Event.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    bool bVisible = true;

private:
    static void DrawObject(const char* Label, Pico::PObject* Object)
    {
        if (Object == nullptr)
        {
            ImGui::TextDisabled("%-18s <none>", Label);
            return;
        }
        const Pico::FObjectHandle Handle = Object->GetHandle();
        ImGui::Text("%-18s %s [%u:%u]",
            Label,
            Object->GetName().ToString().c_str(),
            Handle.Index,
            Handle.Serial);
    }

    void Observe(
        const char* Label,
        Pico::PObject* Object,
        Pico::FObjectHandle& Previous)
    {
        const Pico::FObjectHandle Current = Object != nullptr
            ? Object->GetHandle()
            : Pico::FObjectHandle {};
        if (Current == Previous)
        {
            return;
        }
        if (Previous.IsValid())
        {
            Events.emplace_back(std::string(Label) + " released ["
                + std::to_string(Previous.Index) + ":"
                + std::to_string(Previous.Serial) + "]");
        }
        if (Object != nullptr)
        {
            Events.emplace_back(std::string(Label) + " -> "
                + Object->GetName().ToString() + " ["
                + std::to_string(Current.Index) + ":"
                + std::to_string(Current.Serial) + "]");
        }
        Previous = Current;
        if (Events.size() > 128)
        {
            Events.erase(Events.begin(), Events.begin() + 32);
        }
    }

    std::vector<std::string> Events;
    Pico::FObjectHandle WorldHandle;
    Pico::FObjectHandle GameModeHandle;
    Pico::FObjectHandle GameStateHandle;
    Pico::FObjectHandle LocalPlayerHandle;
    Pico::FObjectHandle ControllerHandle;
    Pico::FObjectHandle PlayerStateHandle;
    Pico::FObjectHandle PawnHandle;
    Pico::FObjectHandle LastPawnHandle;
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
    bool bImGuiContextCreated = false;
    bool bImGuiGlfwInitialized = false;
    bool bImGuiOpenGLInitialized = false;
    int ExitCode = 1;

    try
    {
        bRendererInitialized = Renderer.Initialize(&LoadOpenGLProcedure);
        if (!bRendererInitialized)
        {
            throw std::runtime_error("scene renderer initialization failed");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        bImGuiContextCreated = true;
        ImGuiIO& ImGuiIO = ImGui::GetIO();
        const std::filesystem::path InterfaceFont = "C:/Windows/Fonts/segoeui.ttf";
        if (std::filesystem::is_regular_file(InterfaceFont))
        {
            ImGuiIO.FontDefault = ImGuiIO.Fonts->AddFontFromFileTTF(
                InterfaceFont.string().c_str(), 20.0f);
        }
        if (ImGuiIO.FontDefault == nullptr)
        {
            ImFontConfig FontConfig;
            FontConfig.SizePixels = 20.0f;
            ImGuiIO.FontDefault = ImGuiIO.Fonts->AddFontDefault(&FontConfig);
        }
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 3.0f;
        ImGui::GetStyle().FrameRounding = 2.0f;

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

        bImGuiGlfwInitialized = ImGui_ImplGlfw_InitForOpenGL(Window, true);
        bImGuiOpenGLInitialized = ImGui_ImplOpenGL3_Init("#version 130");
        if (!bImGuiGlfwInitialized || !bImGuiOpenGLInitialized)
        {
            throw std::runtime_error("game debug UI initialization failed");
        }

        FGameplayDebugPanel GameplayDebug;
        bool bF1WasDown = false;

        while (ExitCode == 0
            && !glfwWindowShouldClose(Window)
            && !GameEngine.ShouldExit())
        {
            FInputSystem& Input = GameEngine.GetInputSystem();
            Input.BeginFrame();
            glfwPollEvents();
            const bool bF1Down = glfwGetKey(Window, GLFW_KEY_F1) == GLFW_PRESS;
            if (bF1Down && !bF1WasDown)
            {
                GameplayDebug.bVisible = !GameplayDebug.bVisible;
            }
            bF1WasDown = bF1Down;
            GameEngine.Tick();
            GameplayDebug.Update(GameEngine);

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

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                GameplayDebug.Draw(GameEngine);
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
    if (bImGuiOpenGLInitialized) ImGui_ImplOpenGL3_Shutdown();
    if (bImGuiGlfwInitialized) ImGui_ImplGlfw_Shutdown();
    if (bImGuiContextCreated) ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return ExitCode;
}
}
