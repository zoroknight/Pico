#include "TestRunner.h"

#include "Pico/Core/Config.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Engine/GameInstance.h"
#include "Pico/Engine/GameModeBase.h"
#include "Pico/Engine/GameStateBase.h"
#include "Pico/Engine/GameModule.h"
#include "Pico/Engine/LocalPlayer.h"
#include "Pico/Engine/MatchState.h"
#include "Pico/Engine/NetDriver.h"
#include "Pico/Engine/NetPlayer.h"
#include "Pico/Engine/Pawn.h"
#include "Pico/Engine/PlayerController.h"
#include "Pico/Engine/PlayerState.h"
#include "Pico/Engine/World.h"
#include "Pico/Input/InputSystem.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectSystem.h"
#include "Pico/Net/NetPacket.h"
#include "Pico/Net/LoopbackTransport.h"
#include "PicoSandbox/SandboxModule.h"

#include <filesystem>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
class FFrameOrderTransport final : public Pico::INetTransport
{
public:
    explicit FFrameOrderTransport(Pico::PWorld* InWorld)
        : World(InWorld)
    {
        Pico::FNetByteWriter Writer;
        Writer.WriteUInt8(static_cast<Pico::uint8>(
            Pico::ENetHandshakeType::ClientHello));
        Writer.WriteUInt64(99);
        Pico::FNetPacketHeader Header;
        Header.Flags = Pico::ENetPacketFlags::Handshake;
        Header.Sequence = 1;
        Pico::EncodeNetPacket(Header, Writer.GetBytes(), IncomingPacket);
    }

    bool Open(const Pico::FNetAddress& InLocalAddress) override
    {
        LocalAddress = InLocalAddress;
        bOpen = true;
        return true;
    }

    void Close() override { bOpen = false; }
    bool IsOpen() const override { return bOpen; }

    bool SendTo(
        const Pico::FNetAddress&,
        std::span<const Pico::uint8>) override
    {
        FlushWorldTickCount = World != nullptr ? World->GetTickCount() : 0;
        return true;
    }

    Pico::ENetReceiveResult ReceiveFrom(
        Pico::FNetAddress& OutRemoteAddress,
        std::vector<Pico::uint8>& OutBytes) override
    {
        if (bPacketConsumed) return Pico::ENetReceiveResult::None;
        bPacketConsumed = true;
        DispatchWorldTickCount = World != nullptr ? World->GetTickCount() : 0;
        OutRemoteAddress = { "TestClient", 9001 };
        OutBytes = IncomingPacket;
        return Pico::ENetReceiveResult::Packet;
    }

    const Pico::FNetAddress& GetLocalAddress() const override
    {
        return LocalAddress;
    }

    const std::string& GetLastError() const override { return LastError; }

    Pico::uint64 DispatchWorldTickCount = 0;
    Pico::uint64 FlushWorldTickCount = 0;

private:
    Pico::PWorld* World = nullptr;
    Pico::FNetAddress LocalAddress;
    std::vector<Pico::uint8> IncomingPacket;
    std::string LastError;
    bool bOpen = false;
    bool bPacketConsumed = false;
};

class PTestGameInstance final : public Pico::PGameInstance
{
    PICO_DECLARE_CLASS(PTestGameInstance, Pico::PGameInstance)

public:
    inline static std::vector<std::string> Events;
    inline static Pico::uint64 ObservedWorldTickCount = 0;

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
        if (const Pico::PWorld* World = GetWorld())
        {
            ObservedWorldTickCount = World->GetTickCount();
        }
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

void TestNetDriverMultipleClients(FTestRunner& Runner)
{
    auto Network = std::make_shared<Pico::FLoopbackNetwork>();
    Pico::FNetDriver Server;
    Pico::FNetDriver ClientA;
    Pico::FNetDriver ClientB;
    const Pico::FNetAddress ServerAddress { "0.0.0.0", 7777 };
    const bool bInitialized = Server.InitializeServer(
            ServerAddress.Port,
            std::make_unique<Pico::FLoopbackTransport>(Network))
        && ClientA.InitializeClient(
            ServerAddress,
            std::make_unique<Pico::FLoopbackTransport>(Network))
        && ClientB.InitializeClient(
            ServerAddress,
            std::make_unique<Pico::FLoopbackTransport>(Network));
    Runner.Expect(
        bInitialized,
        "NetDriver creates a loopback server and two ephemeral clients");
    if (!bInitialized) return;

    ClientA.SetNetworkSimulationSettings({120, 15, 5});
    const Pico::FNetworkSimulationSnapshot Simulation =
        ClientA.GetNetworkSimulationSnapshot();
    Runner.Expect(
        Simulation.Settings.LatencyMs == 120
            && Simulation.Settings.JitterMs == 15
            && Simulation.Settings.PacketLossPercent == 5,
        "NetDriver owns per-process latency, jitter, and packet-loss simulation settings");

    for (int Step = 0; Step < 80
        && (Server.GetOpenConnectionCount() != 2
            || ClientA.GetOpenConnectionCount() != 1
            || ClientB.GetOpenConnectionCount() != 1); ++Step)
    {
        Network->AdvanceTime(0.05);
        Server.TickDispatch(0.05f);
        ClientA.TickDispatch(0.05f);
        ClientB.TickDispatch(0.05f);
        Server.TickFlush(0.05f);
        ClientA.TickFlush(0.05f);
        ClientB.TickFlush(0.05f);
    }
    Runner.Expect(
        Server.GetOpenConnectionCount() == 2
            && ClientA.GetOpenConnectionCount() == 1
            && ClientB.GetOpenConnectionCount() == 1,
        "One NetDriver server accepts two independent client connections");

    ClientA.Shutdown();
    for (int Step = 0; Step < 320; ++Step)
    {
        Network->AdvanceTime(0.05);
        Server.TickDispatch(0.05f);
        ClientB.TickDispatch(0.05f);
        Server.TickFlush(0.05f);
        ClientB.TickFlush(0.05f);
    }
    Runner.Expect(
        Server.GetOpenConnectionCount() == 1
            && ClientB.GetOpenConnectionCount() == 1,
        "A timed-out client is removed without affecting another connection");
    ClientB.Shutdown();
    Server.Shutdown();
}

void TestNetDriverUdpMultipleClients(FTestRunner& Runner)
{
    Pico::FNetDriver Server;
    Pico::FNetDriver ClientA;
    Pico::FNetDriver ClientB;
    const bool bServerInitialized = Server.InitializeServer(0);
    const Pico::FNetAddress ServerAddress {
        "127.0.0.1", Server.GetLocalAddress().Port };
    const bool bInitialized = bServerInitialized
        && ClientA.InitializeClient(ServerAddress)
        && ClientB.InitializeClient(ServerAddress);
    Runner.Expect(
        bInitialized,
        "UDP NetDrivers bind one server and two ephemeral localhost clients");
    if (!bInitialized)
    {
        ClientA.Shutdown();
        ClientB.Shutdown();
        Server.Shutdown();
        return;
    }

    for (int Step = 0; Step < 300
        && (Server.GetOpenConnectionCount() != 2
            || ClientA.GetOpenConnectionCount() != 1
            || ClientB.GetOpenConnectionCount() != 1); ++Step)
    {
        Server.TickDispatch(0.01f);
        ClientA.TickDispatch(0.01f);
        ClientB.TickDispatch(0.01f);
        Server.TickFlush(0.01f);
        ClientA.TickFlush(0.01f);
        ClientB.TickFlush(0.01f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Runner.Expect(
        Server.GetOpenConnectionCount() == 2
            && ClientA.GetOpenConnectionCount() == 1
            && ClientB.GetOpenConnectionCount() == 1,
        "Real UDP NetDrivers complete two independent localhost handshakes");
    ClientA.Shutdown();
    ClientB.Shutdown();
    Server.Shutdown();
}

void TestGameInstanceLifecycle(FTestRunner& Runner)
{
    PTestGameInstance::Events.clear();
    PTestGameInstance::ObservedWorldTickCount = 0;
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

    Pico::PGameModeBase* GameMode =
        GameEngine.GetEngineLoop().GetWorld()->GetGameMode();
    Pico::PNetPlayer* NetPlayer = Pico::NewObject<Pico::PNetPlayer>(
        GameInstance, "RegressionNetPlayer");
    Pico::PPlayerController* NetController = GameMode != nullptr
        ? GameMode->Login(NetPlayer) : nullptr;
    const bool bNetworkPlayerStarted = NetController != nullptr
        && GameMode->HandleStartingNewPlayer(NetController);
    Pico::PPawn* NetPawn = NetController != nullptr
        ? NetController->GetPawn() : nullptr;
    Pico::PNetPlayer* SecondNetPlayer = Pico::NewObject<Pico::PNetPlayer>(
        GameInstance, "RegressionNetPlayer_2");
    Pico::PPlayerController* SecondNetController = GameMode != nullptr
        ? GameMode->Login(SecondNetPlayer) : nullptr;
    const bool bSecondNetworkPlayerStarted = SecondNetController != nullptr
        && GameMode->HandleStartingNewPlayer(SecondNetController);
    Pico::PPawn* SecondNetPawn = SecondNetController != nullptr
        ? SecondNetController->GetPawn() : nullptr;
    Runner.Expect(bNetworkPlayerStarted && bSecondNetworkPlayerStarted
            && NetPawn != nullptr && SecondNetPawn != nullptr
            && InitialPawn != nullptr
            && NetPawn != InitialPawn && SecondNetPawn != InitialPawn,
        "Network players spawn new Pawn instances instead of taking the local Auto Possess Pawn");
    Runner.Expect(NetPawn != nullptr && SecondNetPawn != nullptr
            && GameMode != nullptr
            && NetPawn->GetClass() == GameMode->GetDefaultPawnClass()
            && SecondNetPawn->GetClass() == GameMode->GetDefaultPawnClass(),
        "All network players use the configured default Pawn class");
    if (GameMode != nullptr && NetController != nullptr)
        GameMode->Logout(NetController);
    if (GameMode != nullptr && SecondNetController != nullptr)
        GameMode->Logout(SecondNetController);
    if (NetPlayer != nullptr) Pico::DestroyObjectTree(NetPlayer);
    if (SecondNetPlayer != nullptr) Pico::DestroyObjectTree(SecondNetPlayer);

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

    const Pico::uint64 WorldTicksBeforeFrame =
        GameEngine.GetEngineLoop().GetWorld()->GetTickCount();
    auto FrameOrderTransport = std::make_unique<FFrameOrderTransport>(
        GameEngine.GetEngineLoop().GetWorld());
    FFrameOrderTransport* FrameOrderTransportView = FrameOrderTransport.get();
    Runner.Expect(
        GameEngine.GetNetDriver().InitializeServer(
            7777, std::move(FrameOrderTransport)),
        "GameEngine can install a controlled server transport for frame tests");
    GameEngine.Tick();
    Runner.Expect(
        PTestGameInstance::Events.back() == "Tick"
            && GameEngine.GetEngineLoop().GetWorld()->GetGameState()
                ->GetElapsedMatchTime() > 0.0f,
        "GameEngine forwards each frame and advances the active match clock");
    Runner.Expect(
        PTestGameInstance::ObservedWorldTickCount == WorldTicksBeforeFrame
            && GameEngine.GetEngineLoop().GetWorld()->GetTickCount()
                == WorldTicksBeforeFrame + 1,
        "GameInstance ticks in the pre-World phase before Gameplay and physics");
    Runner.Expect(
        FrameOrderTransportView->DispatchWorldTickCount == WorldTicksBeforeFrame
            && FrameOrderTransportView->FlushWorldTickCount
                == WorldTicksBeforeFrame + 1,
        "NetDriver dispatches before the World and flushes after the World");

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
    TestNetDriverMultipleClients(Runner);
    TestNetDriverUdpMultipleClients(Runner);
    TestGameInstanceLifecycle(Runner);
    return Runner.Finish();
}
