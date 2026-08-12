#include "TestRunner.h"

#include "Pico/Core/Config.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/GameModule.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/MatchState.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"
#include "PicoSandbox/SandboxModule.h"

#include <filesystem>
#include <string>
#include <vector>

namespace
{
class PTestGameInstance final : public Pico::PGameInstance
{
    PICO_DECLARE_CLASS(PTestGameInstance, Pico::PGameInstance)

public:
    inline static std::vector<std::string> Events;

    bool Init(Pico::FGameEngine& GameEngine) override
    {
        Events.emplace_back("Init");
        return Pico::PGameInstance::Init(GameEngine);
    }

    void OnWorldInitialized(Pico::PWorld*) override
    {
        Events.emplace_back("WorldInitialized");
    }

    void Tick(float) override
    {
        Events.emplace_back("Tick");
    }

    void OnWorldCleanup(Pico::PWorld*) override
    {
        Events.emplace_back("WorldCleanup");
    }

    void Shutdown() override
    {
        Events.emplace_back("Shutdown");
    }

protected:
    explicit PTestGameInstance(const Pico::FObjectConstructionParams& Params)
        : PGameInstance(Params)
    {
    }
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PTestGameInstance)

class FTestGameModule final : public Pico::IGameModule
{
public:
    bool StartupModule() override
    {
        PTestGameInstance::Events.emplace_back("ModuleStartup");
        return PicoSandbox::RegisterSandboxGameplayClasses()
            && PTestGameInstance::RegisterClass();
    }

    const Pico::PClass* GetGameInstanceClass() const override
    {
        return PTestGameInstance::StaticClass();
    }

    void ShutdownModule() override
    {
        PTestGameInstance::Events.emplace_back("ModuleShutdown");
    }
};

void TestKeyTransitions(FTestRunner& Runner)
{
    Pico::FInputSystem Input;
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::W, true);
    Runner.Expect(Input.IsKeyDown(Pico::EKey::W), "pressed key is down");
    Runner.Expect(Input.WasKeyPressed(Pico::EKey::W), "press transition is recorded");
    Runner.Expect(!Input.WasKeyReleased(Pico::EKey::W), "press is not a release");

    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::W, true);
    Runner.Expect(Input.IsKeyDown(Pico::EKey::W), "held key stays down");
    Runner.Expect(!Input.WasKeyPressed(Pico::EKey::W), "held key does not repeat press");

    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::W, false);
    Runner.Expect(!Input.IsKeyDown(Pico::EKey::W), "released key is up");
    Runner.Expect(Input.WasKeyReleased(Pico::EKey::W), "release transition is recorded");
}

void TestFocusAndPointer(FTestRunner& Runner)
{
    Pico::FInputSystem Input;
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::A, true);
    Input.SetMousePosition(100.0, 200.0);
    Input.SetMousePosition(112.0, 193.0);
    Input.AddMouseWheelDelta(2.0);
    Runner.Expect(
        Input.GetMouseDelta().Equals(Pico::FVector2(12.0f, -7.0f)),
        "mouse delta accumulates within a frame");
    Runner.Expect(Input.GetMouseWheelDelta() == 2.0f, "mouse wheel accumulates");

    Input.SetFocused(false);
    Runner.Expect(!Input.IsKeyDown(Pico::EKey::A), "focus loss clears held keys");
    Runner.Expect(Input.WasKeyReleased(Pico::EKey::A), "focus loss records release");

    Input.BeginFrame();
    Runner.Expect(
        Input.GetMouseDelta().Equals(Pico::FVector2()),
        "mouse delta resets each frame");
    Runner.Expect(Input.GetMouseWheelDelta() == 0.0f, "mouse wheel resets each frame");
}

void TestMappings(FTestRunner& Runner)
{
    Pico::FConfigFile Config;
    Runner.Expect(
        Config.Load(std::filesystem::path("Tests/Fixtures/Input.ini")),
        "input fixture loads");

    Pico::FInputSystem Input;
    Input.LoadMappings(Config);
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::Enter, true);
    Runner.Expect(Input.IsActionDown("Jump"), "an action supports multiple keys");
    Runner.Expect(Input.WasActionPressed("Jump"), "mapped action reports press");

    Input.SetKeyState(Pico::EKey::W, true);
    Runner.Expect(Input.GetAxisValue("MoveForward") == 1.0f, "positive axis mapping works");
    Input.SetKeyState(Pico::EKey::S, true);
    Runner.Expect(Input.GetAxisValue("MoveForward") == 0.0f, "opposite keys cancel");
    Input.SetKeyState(Pico::EKey::W, false);
    Runner.Expect(Input.GetAxisValue("MoveForward") == -1.0f, "negative axis mapping works");

    Input.SetMousePosition(10.0, 10.0);
    Input.SetMousePosition(14.0, 7.0);
    Runner.Expect(Input.GetAxisValue("LookYaw") == 4.0f, "mouse axis uses sensitivity");
    Runner.Expect(Input.GetAxisValue("LookPitch") == 1.5f, "mouse axis supports inversion");
}

void TestDefaultMappings(FTestRunner& Runner)
{
    Pico::FConfigFile Config;
    Runner.Expect(
        Config.Load(std::filesystem::path("Config/Pico.ini")),
        "engine config loads");
    Pico::FInputSystem Input;
    Input.LoadMappings(Config);
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::Space, true);
    Input.SetKeyState(Pico::EKey::D, true);
    Runner.Expect(Input.IsActionDown("Jump"), "missing action gets runtime default");
    Runner.Expect(Input.GetAxisValue("MoveRight") == 1.0f, "missing axis gets runtime default");
}

void TestContentPathResolution(FTestRunner& Runner)
{
    const std::filesystem::path ContentRoot =
        std::filesystem::absolute("Projects/PicoSandbox/Content");
    std::filesystem::path Resolved;
    Runner.Expect(
        Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "/Game/Maps/EditorWorld.pworld", Resolved),
        "valid default map resolves under Content");
    Runner.Expect(
        Resolved.filename() == "EditorWorld.pworld",
        "resolved map preserves the configured file");
    Runner.Expect(
        !Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "/Game/../PicoSandbox.pico", Resolved),
        "default map cannot escape Content");
    Runner.Expect(
        !Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "/Game/Maps/Missing.pworld", Resolved),
        "missing default map fails safely");
    Runner.Expect(
        !Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "Maps/EditorWorld.pworld", Resolved),
        "default map requires a Game virtual path");
}

void TestGameInstanceLifecycle(FTestRunner& Runner)
{
    PTestGameInstance::Events.clear();
    FTestGameModule Module;
    Pico::FGameEngine GameEngine(&Module);
    char Program[] = "PicoGameTests";
    char MaxFPS[] = "-maxfps=0";
    char* Arguments[] = { Program, MaxFPS };
    const std::filesystem::path ProjectFile =
        std::filesystem::absolute("Projects/PicoSandbox/PicoSandbox.pico");

    const bool bInitialized = GameEngine.PreInit(2, Arguments, ProjectFile) == 0
        && GameEngine.Init() == 0;
    Runner.Expect(bInitialized, "GameEngine initializes a reflected project GameInstance");
    if (!bInitialized)
    {
        GameEngine.Exit();
        return;
    }

    PTestGameInstance* GameInstance =
        static_cast<PTestGameInstance*>(GameEngine.GetGameInstance());
    Runner.Expect(
        GameInstance != nullptr
            && GameInstance->GetClass() == PTestGameInstance::StaticClass()
            && GameInstance->GetGameEngine() == &GameEngine
            && GameInstance->GetWorld() == GameEngine.GetEngineLoop().GetWorld(),
        "GameInstance is constructed through PClass and bound to the loaded World");
    Pico::PLocalPlayer* LocalPlayer = GameInstance->GetPrimaryLocalPlayer();
    const Pico::FObjectHandle LocalPlayerHandle =
        LocalPlayer != nullptr ? LocalPlayer->GetHandle() : Pico::FObjectHandle {};
    Pico::PPlayerController* InitialController = LocalPlayer != nullptr
        ? LocalPlayer->GetPlayerController()
        : nullptr;
    Pico::PPlayerState* InitialPlayerState = InitialController != nullptr
        ? InitialController->GetPlayerState()
        : nullptr;
    Pico::PPawn* InitialPawn = InitialController != nullptr
        ? InitialController->GetPawn()
        : nullptr;
    const Pico::FObjectHandle InitialControllerHandle = InitialController != nullptr
        ? InitialController->GetHandle() : Pico::FObjectHandle {};
    const Pico::FObjectHandle InitialPlayerStateHandle = InitialPlayerState != nullptr
        ? InitialPlayerState->GetHandle() : Pico::FObjectHandle {};
    const Pico::FObjectHandle InitialPawnHandle = InitialPawn != nullptr
        ? InitialPawn->GetHandle() : Pico::FObjectHandle {};
    Runner.Expect(
        LocalPlayer != nullptr
            && LocalPlayer->GetOuter() == GameInstance
            && LocalPlayer->GetLocalPlayerIndex() == 0
            && InitialController != nullptr
            && InitialController->GetPlayer() == LocalPlayer
            && InitialPlayerState != nullptr
            && InitialPawn != nullptr
            && InitialPawn->GetController() == InitialController
            && GameEngine.GetEngineLoop().GetWorld()->GetGameMode() != nullptr
            && GameEngine.GetEngineLoop().GetWorld()->GetGameState() != nullptr
            && GameEngine.GetEngineLoop().GetWorld()->GetGameState()
                ->GetPlayerStates().size() == 1
            && GameEngine.GetEngineLoop().GetWorld()->GetGameState()
                ->GetMatchState() == Pico::EMatchState::InProgress,
        "GameInstance logs its persistent LocalPlayer into the World Gameplay chain");
    Runner.Expect(
        PTestGameInstance::Events
            == std::vector<std::string>({"ModuleStartup", "Init", "WorldInitialized"}),
        "GameInstance initialization happens before its World initialization callback");

    const Pico::FObjectHandle GameInstanceHandle = GameInstance->GetHandle();
    Pico::CollectGarbage();
    Runner.Expect(
        Pico::ResolveObject(GameInstanceHandle) == GameInstance,
        "The rooted GameInstance survives a full garbage collection");
    Runner.Expect(
        Pico::ResolveObject(LocalPlayerHandle) == LocalPlayer,
        "GameInstance keeps its LocalPlayer reachable during garbage collection");
    Runner.Expect(
        Pico::ResolveObject(InitialControllerHandle) == InitialController
            && Pico::ResolveObject(InitialPlayerStateHandle) == InitialPlayerState
            && Pico::ResolveObject(InitialPawnHandle) == InitialPawn,
        "World Gameplay ownership keeps the logged-in player chain reachable during garbage collection");

    GameEngine.Tick();
    Runner.Expect(
        PTestGameInstance::Events.back() == "Tick"
            && GameEngine.GetEngineLoop().GetWorld()->GetGameState()
                ->GetElapsedMatchTime() > 0.0f,
        "GameEngine forwards each frame and advances the active match clock");

    Runner.Expect(
        GameEngine.LoadMap(GameEngine.GetDefaultMapPath()),
        "GameEngine can replace its active map through the GameInstance lifecycle");
    Runner.Expect(
        PTestGameInstance::Events.size() >= 6
            && PTestGameInstance::Events[PTestGameInstance::Events.size() - 2]
                == "WorldCleanup"
            && PTestGameInstance::Events.back() == "WorldInitialized",
        "Map replacement cleans up the old World before initializing the new World");
    Runner.Expect(
        GameEngine.GetGameInstance()->GetPrimaryLocalPlayer() == LocalPlayer
            && Pico::ResolveObject(LocalPlayerHandle) == LocalPlayer
            && LocalPlayer->GetPlayerController() != nullptr
            && LocalPlayer->GetPlayerController() != InitialController
            && Pico::ResolveObject(InitialControllerHandle) == nullptr
            && Pico::ResolveObject(InitialPlayerStateHandle) == nullptr
            && Pico::ResolveObject(InitialPawnHandle) == nullptr,
        "LocalPlayer survives map replacement while its World Gameplay actors are recreated");

    GameEngine.Exit();
    Runner.Expect(
        PTestGameInstance::Events
            == std::vector<std::string>({
                "ModuleStartup",
                "Init",
                "WorldInitialized",
                "Tick",
                "WorldCleanup",
                "WorldInitialized",
                "WorldCleanup",
                "Shutdown",
                "ModuleShutdown"}),
        "GameInstance cleanup finishes before the project module shuts down");
    Runner.Expect(
        !Pico::PObjectSystem::IsInitialized()
            && GameEngine.GetGameInstance() == nullptr,
        "GameEngine exit destroys GameInstance and shuts down the object system");
}
}

int main()
{
    FTestRunner Runner;
    TestKeyTransitions(Runner);
    TestFocusAndPointer(Runner);
    TestMappings(Runner);
    TestDefaultMappings(Runner);
    TestContentPathResolution(Runner);
    TestGameInstanceLifecycle(Runner);
    return Runner.Finish();
}
